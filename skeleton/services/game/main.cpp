// game: Riftbound against Dev (Phase 5, D21). So far the card data, decks (5.1) and the rules engine as it grows
// (5.2); AI and MQTT follow.
//
//   buddy-game --deck <name|file> --check   list a deck card by card, check it against the deck rules
//   buddy-game --check                      the same for every deck in BUDDY_GAME_DECKS
//   buddy-game --code <code>…               what a printed code is ("OGN-007a/298", "VEN • R01 • EN")
//   buddy-game --random [--games N] [--seed S] [--deck A --deck B]
//                                           random-vs-random games in vanilla mode (default: the first two decks),
//                                           seeds S … S+N-1, invariants checked after every action; a failing game
//                                           writes replay-<seed>.txt; then sizeof(GameState) and its copy time
//   buddy-game --replay <file>              play a replay action by action with the same checks
//
// Config (env, see deploy/config/buddy.env.example): BUDDY_GAME_CARDS, BUDDY_GAME_DECKS
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "buddy/config.hpp"
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
    std::cout << std::format("  {:<12} {:>2}  {:<32} {:<11} {:>2} {:>2} {:>2}  {:<11} {}\n", section, count, c.name,
                             to_string(c.type), stat(c.energy), stat(c.power), stat(c.might),
                             domains_string(c.domains), printings(c));
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
        std::cout << std::format("  {:<12} {:>2}  {:<32} {:<11} {:>2} {:>2} {:>2}  {:<11} {}\n", "", "n", "card",
                                 "type", "E", "P", "M", "domains", "printings");
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
void measure_copies(const Deck& a, const Deck& b, std::uint64_t seed, const std::vector<Action>& log) {
    GameState s = new_game(a, b, {.seed = seed, .vanilla = true});
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

int random_games(const CardDb& db, const std::vector<fs::path>& paths, int games, std::uint64_t seed) {
    if (paths.size() < 2) throw std::runtime_error("--random needs two decks");
    const Deck a = load_deck(db, paths[0]), b = load_deck(db, paths[1]);
    std::cout << std::format("random games, vanilla: seat 0 {} vs seat 1 {}, seeds {}–{}\n",
                             paths[0].stem().string(), paths[1].stem().string(), seed, seed + games - 1);
    std::array<int, player_count> wins{};
    int failed = 0, min_turns = 0, max_turns = 0;
    long turns = 0, actions = 0, spells = 0, reactions = 0, moves = 0, showdowns = 0, combats = 0, scorings = 0;
    std::optional<RandomGame> sample;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < games; ++i) {
        RandomGame g = play_random(a, b, seed + i);
        if (!g.problem.empty()) {
            const fs::path file = fs::absolute(std::format("replay-{}.txt", seed + i));
            const Replay replay{seed + i, true, {read_file(paths[0]), read_file(paths[1])}, g.log};
            std::ofstream(file) << to_text(replay, db);
            std::cerr << std::format("✗ seed {}: {}\n  replay: {}\n", seed + i, g.problem, file.string());
            ++failed;
            continue;
        }
        ++wins[*g.winner];
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
    std::cout << std::format("  wins: seat 0 {}, seat 1 {}; failed {}\n", wins[0], wins[1], failed);
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
    if (sample) measure_copies(a, b, seed, sample->log);
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

int usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " --deck <name|file> --check | --check | --code <code>… |\n"
              << "       --random [--games N] [--seed S] [--deck A --deck B] | --replay <file>\n"
              << "(the game service itself comes in step 5.6)\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::vector<std::string> deck_args;
    bool check = false, random = false;
    std::string replay;
    int code_index = 0, games = 100;
    std::uint64_t seed = 1;
    try {
        for (std::size_t i = 0; i < args.size() && code_index == 0; ++i) {
            const bool value = i + 1 < args.size();
            if (args[i] == "--deck" && value) deck_args.push_back(args[++i]);
            else if (args[i] == "--check") check = true;
            else if (args[i] == "--random") random = true;
            else if (args[i] == "--replay" && value) replay = args[++i];
            else if (args[i] == "--games" && value) games = std::stoi(args[++i]);
            else if (args[i] == "--seed" && value) seed = std::stoull(args[++i]);
            else if (args[i] == "--code" && value) code_index = static_cast<int>(i) + 2;
            else return usage(argv[0]);
        }
    } catch (const std::logic_error&) {  // stoi/stoull
        return usage(argv[0]);
    }
    if ((code_index != 0) + check + random + !replay.empty() != 1 || games < 1) return usage(argv[0]);

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
        if (random) return random_games(db, paths, games, seed);

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
