// game: Riftbound against Dev (Phase 5, D21). So far the card data, decks (5.1), the rules engine (5.2), the decks'
// cards (5.3) and the AI as it grows (5.4); MQTT follows.
//
//   buddy-game --deck <name|file> --check   list a deck card by card, check it against the deck rules
//   buddy-game --check                      the same for every deck in BUDDY_GAME_DECKS
//   buddy-game --code <code>…               what a printed code is ("OGN-007a/298", "VEN • R01 • EN")
//   buddy-game --random [--rules] [--games N] [--seed S] [--deck A --deck B]
//                                           random-vs-random games in vanilla mode, with --rules the cards' rules
//                                           (every card must be implemented; default: the first two decks),
//                                           seeds S … S+N-1, invariants checked after every action; a failing game
//                                           writes replay-<seed>.txt; a digest of all games (same games = same
//                                           digest), the engine-only speed, sizeof(GameState) and its copy time
//   buddy-game --replay <file>              play a replay action by action with the same checks
//   buddy-game --match <a> <b> [--games N] [--seed S] [--jobs J] [--deck A --deck B]
//                                           AI players against each other with the cards' rules (players:
//                                           random, greedy): both decks from both seats, wins with the 95 % Wilson
//                                           interval, decision times, branching; a failing game writes its replay
//
// Config (env, see deploy/config/buddy.env.example): BUDDY_GAME_CARDS, BUDDY_GAME_DECKS
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

#include "buddy/config.hpp"
#include "riftbound/ai/arena.hpp"
#include "riftbound/ai/greedy.hpp"
#include "riftbound/card_db.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/replay.hpp"

namespace {

using namespace buddy;
using namespace buddy::riftbound;
namespace fs = std::filesystem;

std::string stat(const std::optional<int>& v) { return v ? std::to_string(*v) : "-"; }

std::string printings(const Card& c) {
    std::string s;
    for (const auto& p : c.printings) s += (s.empty() ? "" : " ") + p.label;
    return s;
}

void print_row(std::string_view section, int count, const Card& c) {
    std::cout << std::format("  {:<12} {:>2}  {:<32} {:<11} {:>2} {:>2} {:>2}  {:<11} {:<5} {}\n", section, count,
                             c.name, to_string(c.type), stat(c.energy), stat(c.power), stat(c.might),
                             domains_string(c.domains), is_implemented(c) ? "yes" : "-", printings(c));
}

// Name in the decks folder ("zed-vendetta") or a path.
fs::path deck_path(const std::string& arg, const fs::path& dir) {
    if (fs::is_regular_file(arg)) return arg;
    for (const fs::path& p : {dir / arg, dir / (arg + ".txt")}) {
        if (fs::is_regular_file(p)) return p;
    }
    throw std::runtime_error("no deck \"" + arg + "\" (a file, or a name in " + dir.string() + ")");
}

std::vector<fs::path> all_decks(const fs::path& dir) {
    std::vector<fs::path> decks;
    if (fs::is_directory(dir)) {
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".txt") decks.push_back(e.path());
        }
    }
    std::sort(decks.begin(), decks.end());
    if (decks.empty()) throw std::runtime_error("no decks (*.txt) in " + dir.string());
    return decks;
}

bool check_deck(const CardDb& db, const fs::path& path) {
    try {
        const Deck deck = load_deck(db, path);
        std::cout << std::format("✓ {}: {}, {} cards (main deck {} with the champion, {} runes, {} battlefields)\n",
                                 path.filename().string(), deck.legend->name,
                                 1 + deck.main_size() + deck.rune_count() + static_cast<int>(deck.battlefields.size()),
                                 deck.main_size(), deck.rune_count(), deck.battlefields.size());
        std::cout << std::format("  {:<12} {:>2}  {:<32} {:<11} {:>2} {:>2} {:>2}  {:<11} {:<5} {}\n", "", "n",
                                 "card", "type", "E", "P", "M", "domains", "rules", "printings");
        print_row("Legend", 1, *deck.legend);
        print_row("Champion", 1, *deck.champion);
        for (const auto& e : deck.main) print_row("MainDeck", e.count, *e.card);
        for (const Card* b : deck.battlefields) print_row("Battlefields", 1, *b);
        for (const auto& e : deck.runes) print_row("Runes", e.count, *e.card);
        return true;
    } catch (const DeckError& e) {
        std::cerr << "✗ " << path.filename().string() << ":\n";
        for (const auto& p : e.problems()) std::cerr << "  " << p << "\n";
        return false;
    }
}

int codes(const CardDb& db, int argc, char** argv, int first) {
    int failed = 0;
    for (int i = first; i < argc; ++i) {
        const auto found = db.find_code(argv[i]);
        if (found.card == nullptr) {
            std::cerr << "✗ \"" << argv[i] << "\": " << found.error << "\n";
            ++failed;
            continue;
        }
        std::cout << std::format("\"{}\" → {} {} ({}, {}; {})\n", argv[i], found.printing->label, found.card->name,
                                 to_string(found.card->type), domains_string(found.card->domains),
                                 found.printing->full_name);
    }
    return failed == 0 ? 0 : 1;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("can't read " + path.string());
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// What 5.4's search pays per node: the size of a state and the time to copy one, over every state of one game.
void measure_copies(const Deck& a, const Deck& b, std::uint64_t seed, const std::vector<Action>& log, bool vanilla) {
    GameState s = new_game(a, b, {.seed = seed, .vanilla = vanilla});
    constexpr int repeats = 20;
    std::size_t objects = 0;
    double ns = 0;
    for (const Action& action : log) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) {
            const GameState copy = s;
            objects += copy.objects.size();  // keeps the copy alive
        }
        ns += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        apply(s, action);
    }
    const double copies = static_cast<double>(log.size()) * repeats;
    std::cout << std::format("  state: sizeof(GameState) {} B, {:.0f} objects, copy {:.2f} µs (avg over {} states)\n",
                             sizeof(GameState), static_cast<double>(objects) / copies, ns / copies / 1000,
                             log.size());
}

// What a playout pays per action: the same games again, legal_actions + apply only (no checks, no player).
void measure_engine(const Deck& a, const Deck& b, std::uint64_t seed, const std::vector<std::vector<Action>>& logs,
                    bool vanilla) {
    long actions = 0;
    std::size_t offered = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < logs.size(); ++i) {
        if (logs[i].empty()) continue;  // failed game
        GameState s = new_game(a, b, {.seed = seed + i, .vanilla = vanilla});
        for (const Action& action : logs[i]) {
            offered += legal_actions(s).size();
            apply(s, action);
        }
        actions += static_cast<long>(logs[i].size());
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << std::format("  engine only: {:.0f} actions/s ({:.2f} µs per action, {:.1f} legal actions avg)\n",
                             static_cast<double>(actions) / ms * 1000, ms * 1000 / static_cast<double>(actions),
                             static_cast<double>(offered) / static_cast<double>(actions));
}

int random_games(const CardDb& db, const std::vector<fs::path>& paths, int games, std::uint64_t seed, bool vanilla) {
    if (paths.size() < 2) throw std::runtime_error("--random needs two decks");
    const Deck a = load_deck(db, paths[0]), b = load_deck(db, paths[1]);
    std::cout << std::format("random games, {}: seat 0 {} vs seat 1 {}, seeds {}–{}\n",
                             vanilla ? "vanilla" : "card rules", paths[0].stem().string(), paths[1].stem().string(), seed, seed + games - 1);
    std::array<int, player_count> wins{};
    int failed = 0, min_turns = 0, max_turns = 0;
    long turns = 0, actions = 0, spells = 0, reactions = 0, moves = 0, showdowns = 0, combats = 0, scorings = 0;
    std::optional<RandomGame> sample;
    std::vector<std::vector<Action>> logs(static_cast<std::size_t>(games));
    std::uint64_t digest = 0xcbf29ce484222325;  // FNV-1a over every game's winner, turns and actions
    const auto mix = [&](std::uint64_t v) { digest = (digest ^ v) * 0x100000001b3; };
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < games; ++i) {
        RandomGame g = play_random(a, b, seed + i, 100000, vanilla);
        if (!g.problem.empty()) {
            const fs::path file = fs::absolute(std::format("replay-{}.txt", seed + i));
            const Replay replay{seed + i, vanilla, {read_file(paths[0]), read_file(paths[1])}, g.log};
            std::ofstream(file) << to_text(replay, db);
            std::cerr << std::format("✗ seed {}: {}\n  replay: {}\n", seed + i, g.problem, file.string());
            ++failed;
            continue;
        }
        ++wins[*g.winner];
        mix(*g.winner);
        mix(static_cast<std::uint64_t>(g.turns));
        for (const Action& a : g.log) mix(static_cast<std::uint64_t>(a.kind) << 24 | std::uint64_t{a.obj} << 8 | a.n);
        logs[static_cast<std::size_t>(i)] = g.log;
        turns += g.turns;
        actions += g.actions;
        spells += g.spells;
        reactions += g.reactions;
        moves += g.moves;
        showdowns += g.showdowns;
        combats += g.combats;
        scorings += g.scorings;
        min_turns = min_turns == 0 ? g.turns : std::min(min_turns, g.turns);
        max_turns = std::max(max_turns, g.turns);
        if (!sample) sample = std::move(g);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const int ok = games - failed;
    std::cout << std::format("  wins: seat 0 {}, seat 1 {}; failed {}; digest {:016x}\n", wins[0], wins[1], failed,
                             digest);
    if (ok > 0) {
        std::cout << std::format("  turns: avg {:.1f}, min {}, max {}; actions avg {:.1f}\n",
                                 static_cast<double>(turns) / ok, min_turns, max_turns,
                                 static_cast<double>(actions) / ok);
        std::cout << std::format("  per game: {:.1f} spells, {:.1f} cards played on a chain\n",
                                 static_cast<double>(spells) / ok, static_cast<double>(reactions) / ok);
        std::cout << std::format("  per game: {:.1f} moves, {:.1f} showdowns ({:.1f} combats), {:.1f} scorings\n",
                                 static_cast<double>(moves) / ok, static_cast<double>(showdowns) / ok,
                                 static_cast<double>(combats) / ok, static_cast<double>(scorings) / ok);
    }
    std::cout << std::format("  time: {:.2f} ms per game, {:.0f} games/s, {:.0f} actions/s\n", ms / games,
                             games / ms * 1000, static_cast<double>(actions) / ms * 1000);
    measure_engine(a, b, seed, logs, vanilla);
    if (sample) measure_copies(a, b, seed, sample->log, vanilla);
    return failed == 0 ? 0 : 1;
}

int replay_file(const CardDb& db, const fs::path& path) {
    const Replay replay = parse_replay(read_file(path));
    std::cout << std::format("replay {}: seed {}{}, {} actions\n", path.string(), replay.seed,
                             replay.vanilla ? ", vanilla" : "", replay.actions.size());
    const ReplayResult r = run_replay(replay, db, [](const GameState& s, const Action& a) {
        std::cout << std::format("  turn {:>2}  player {}  {}\n", s.turn, s.decision.player, describe(s, a));
    });
    if (!r.problem.empty()) {
        std::cerr << "✗ " << r.problem << "\n";
        return 1;
    }
    const GameState& s = r.state;
    if (s.winner) {
        std::cout << std::format("✓ player {} wins {}–{} on turn {}\n", *s.winner, s.player(*s.winner).points,
                                 s.player(opponent(*s.winner)).points, s.turn);
    } else {
        std::cout << std::format("✓ all {} actions, no winner yet (turn {}, {} for player {})\n", r.applied, s.turn,
                                 to_string(s.decision.kind), s.decision.player);
    }
    return 0;
}

ai::PlayerFactory player_factory(const std::string& name) {
    if (name == "random") return [](std::uint64_t seed) { return std::make_unique<ai::RandomPlayer>(seed); };
    if (name == "greedy") return [](std::uint64_t seed) { return std::make_unique<ai::GreedyPlayer>(seed); };
    throw std::runtime_error("no player \"" + name + "\" (random, greedy)");
}

std::string interval(int wins, int n) {
    const auto [low, high] = ai::wilson(wins, n);
    return std::format("{}/{} ({:.1f} %, 95 % CI {:.1f}–{:.1f} %)", wins, n, n ? 100.0 * wins / n : 0.0, 100 * low,
                       100 * high);
}

int match(const CardDb& db, const std::vector<fs::path>& paths, const std::array<std::string, 2>& names, int games,
          std::uint64_t seed, int jobs) {
    if (paths.size() < 2) throw std::runtime_error("--match needs two decks");
    const Deck deck_a = load_deck(db, paths[0]), deck_b = load_deck(db, paths[1]);
    const std::array<std::string, 2> decks{paths[0].stem().string(), paths[1].stem().string()};
    std::cout << std::format("match {} (A) vs {} (B): {} games, decks {} and {}, seeds {}–{}, {} jobs\n", names[0],
                             names[1], games, decks[0], decks[1], seed, ai::pairing(seed, games - 1).seed, jobs);
    const auto start = std::chrono::steady_clock::now();
    const auto results = ai::play_match(player_factory(names[0]), player_factory(names[1]), deck_a, deck_b, games,
                                        seed, jobs);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    int failed = 0, ok = 0;
    std::array<int, 2> player_wins{}, deck_wins{};
    std::array<std::array<int, 2>, 2> with_deck{}, with_deck_n{};  // [player][deck]
    std::array<std::vector<double>, 2> ms;
    std::array<long, 2> decisions{};
    long forced = 0, all = 0, turns = 0;
    std::vector<double> legal, runs;
    for (const auto& r : results) {
        const ai::ArenaGame& g = r.game;
        if (!g.problem.empty()) {
            const fs::path file = fs::absolute(std::format("replay-{}.txt", r.pairing.seed));
            const Replay replay{r.pairing.seed, false,
                                {read_file(paths[r.pairing.decks_swapped ? 1 : 0]),
                                 read_file(paths[r.pairing.decks_swapped ? 0 : 1])},
                                g.log};
            std::ofstream(file) << to_text(replay, db);
            std::cerr << std::format("✗ seed {}: {}\n  replay: {}\n", r.pairing.seed, g.problem, file.string());
            ++failed;
            continue;
        }
        ++ok;
        turns += g.turns;
        const int wp = *r.winning_player(), wd = *r.winning_deck();
        ++player_wins[wp];
        ++deck_wins[wd];
        for (int p = 0; p < 2; ++p) {
            const int deck_of_p = (p == 0) == r.pairing.decks_swapped ? 1 : 0;  // A plays deck B when swapped
            ++with_deck_n[p][deck_of_p];
            with_deck[p][deck_of_p] += wp == p;
        }
        for (PlayerId seat = 0; seat < player_count; ++seat) {
            const ai::SeatStats& st = g.seats[seat];
            const int p = (seat == 0) == r.pairing.b_in_seat0 ? 1 : 0;
            ms[p].insert(ms[p].end(), st.ms.begin(), st.ms.end());
            decisions[p] += static_cast<long>(st.ms.size());
            forced += st.forced;
            all += static_cast<long>(st.legal.size());
            for (const int n : st.legal) legal.push_back(n);
            for (const int n : st.choose_runs) runs.push_back(n);
        }
    }
    std::cout << std::format("  failed {}\n", failed);
    if (ok == 0) return 1;
    for (int p = 0; p < 2; ++p) {
        std::cout << std::format("  {} {}: wins {}; with {} {}/{}, with {} {}/{}\n", p == 0 ? "A" : "B", names[p],
                                 interval(player_wins[p], ok), decks[0], with_deck[p][0], with_deck_n[p][0],
                                 decks[1], with_deck[p][1], with_deck_n[p][1]);
    }
    std::cout << std::format("  decks: {} {}, {} {}; turns avg {:.1f}\n", decks[0], deck_wins[0], decks[1],
                             deck_wins[1], static_cast<double>(turns) / ok);
    for (int p = 0; p < 2; ++p) {
        std::cout << std::format("  {} decision time: p50 {:.3f} ms, p95 {:.3f} ms, max {:.3f} ms ({:.1f} asked per game)\n",
                                 p == 0 ? "A" : "B", ai::percentile(ms[p], 50), ai::percentile(ms[p], 95),
                                 ai::percentile(ms[p], 100), static_cast<double>(decisions[p]) / ok);
    }
    std::cout << std::format("  branching: {:.1f} decisions per game, {:.1f} % forced; legal actions p50 {:.0f}, p95 "
                             "{:.0f}, max {:.0f}; Choose depth p50 {:.0f}, p95 {:.0f}, max {:.0f}\n",
                             static_cast<double>(all) / ok, 100.0 * static_cast<double>(forced) / static_cast<double>(all),
                             ai::percentile(legal, 50), ai::percentile(legal, 95), ai::percentile(legal, 100),
                             ai::percentile(runs, 50), ai::percentile(runs, 95), ai::percentile(runs, 100));
    std::cout << std::format("  time: {:.1f} s, {:.1f} ms per game per job\n", s, s * 1000 * jobs / games);
    return failed == 0 ? 0 : 1;
}

int usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " --deck <name|file> --check | --check | --code <code>… |\n"
              << "       --random [--rules] [--games N] [--seed S] [--deck A --deck B] | --replay <file> |\n"
              << "       --match <a> <b> [--games N] [--seed S] [--jobs J] [--deck A --deck B]  (players: random, greedy)\n"
              << "(the game service itself comes in step 5.6)\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::vector<std::string> deck_args;
    bool check = false, random = false, rules = false;
    std::string replay;
    std::array<std::string, 2> players;
    bool games_given = false;
    int code_index = 0, games = 100, jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::uint64_t seed = 1;
    try {
        for (std::size_t i = 0; i < args.size() && code_index == 0; ++i) {
            const bool value = i + 1 < args.size();
            if (args[i] == "--deck" && value) deck_args.push_back(args[++i]);
            else if (args[i] == "--check") check = true;
            else if (args[i] == "--random") random = true;
            else if (args[i] == "--rules") rules = true;
            else if (args[i] == "--replay" && value) replay = args[++i];
            else if (args[i] == "--match" && i + 2 < args.size()) {
                players = {args[i + 1], args[i + 2]};
                i += 2;
            }
            else if (args[i] == "--games" && value) {
                games = std::stoi(args[++i]);
                games_given = true;
            }
            else if (args[i] == "--jobs" && value) jobs = std::stoi(args[++i]);
            else if (args[i] == "--seed" && value) seed = std::stoull(args[++i]);
            else if (args[i] == "--code" && value) code_index = static_cast<int>(i) + 2;
            else return usage(argv[0]);
        }
    } catch (const std::logic_error&) {  // stoi/stoull
        return usage(argv[0]);
    }
    const bool matching = !players[0].empty();
    if ((code_index != 0) + check + random + !replay.empty() + matching != 1 || games < 1 || jobs < 1 ||
        (rules && !random)) {
        return usage(argv[0]);
    }

    try {
        const std::string home = env_string("HOME", ".");
        const fs::path cards = env_string("BUDDY_GAME_CARDS", home + "/buddy/data/riftbound/cards.json");
        const fs::path decks = env_string("BUDDY_GAME_DECKS", home + "/buddy/share/game/decks");

        const CardDb db = CardDb::load(cards);
        std::cout << std::format("{}: {} cards, {} printings, fetched {}\n", cards.string(), db.cards().size(),
                                 db.printing_count(), db.fetched());
        for (const auto& w : db.warnings()) std::cerr << "  ⚠ " << w << "\n";
        if (code_index != 0) return codes(db, argc, argv, code_index);
        if (!replay.empty()) return replay_file(db, replay);

        std::vector<fs::path> paths;
        for (const auto& d : deck_args) paths.push_back(deck_path(d, decks));
        if (paths.empty()) paths = all_decks(decks);
        if (random) return random_games(db, paths, games, seed, !rules);
        if (matching) return match(db, paths, players, games_given ? games : 200, seed, jobs);

        bool ok = true;
        for (const auto& path : paths) {
            ok = check_deck(db, path) && ok;
        }
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "✗ " << e.what() << "\n";
        return 1;
    }
}
