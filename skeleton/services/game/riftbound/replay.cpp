#include "replay.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <sstream>

#include "deck.hpp"
#include "engine.hpp"
#include "random_game.hpp"

namespace buddy::riftbound {

namespace {

constexpr std::array<std::string_view, 7> kind_names{"pick", "done", "play", "end-turn", "pass", "activate", "move"};

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

std::vector<std::string_view> words(std::string_view s) {
    std::vector<std::string_view> out;
    while (!(s = trim(s)).empty()) {
        const auto end = s.find_first_of(" \t");
        out.push_back(s.substr(0, end));
        s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    }
    return out;
}

template <class T>
bool number(std::string_view s, T& out) {
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && end == s.data() + s.size();
}

Action parse_action(std::string_view line) {
    const auto w = words(line);
    Action a;
    const auto kind = std::find(kind_names.begin(), kind_names.end(), w.at(0));
    if (kind == kind_names.end()) throw std::invalid_argument("unknown action \"" + std::string(w[0]) + "\"");
    a.kind = static_cast<ActionKind>(kind - kind_names.begin());
    if (w.size() > 3 || (w.size() > 1 && w[1] != "-" && !number(w[1], a.obj)) || (w.size() > 2 && !number(w[2], a.n))) {
        throw std::invalid_argument("expected <kind> [<obj>|-] [<n>]");
    }
    return a;
}

std::string format_action(const Action& a) {
    std::string out(kind_names.at(static_cast<std::size_t>(a.kind)));
    if (a.obj != no_obj || a.n != 0) out += " " + (a.obj == no_obj ? std::string("-") : std::to_string(a.obj));
    if (a.n != 0) out += " " + std::to_string(a.n);
    return out;
}

} // namespace

Replay parse_replay(std::string_view text) {
    Replay r;
    enum { Header, InDeck, InActions } section = Header;
    int seat = 0, line_no = 0;
    std::array<bool, player_count> have_deck{};
    bool have_seed = false;
    std::istringstream in{std::string(text)};
    for (std::string raw; std::getline(in, raw);) {
        ++line_no;
        const std::string where = "line " + std::to_string(line_no) + ": ";
        const std::string_view line = trim(std::string_view(raw).substr(0, raw.find('#')));
        if (section == InDeck) {  // decklist lines as they are, comments included
            if (trim(raw) == "end") section = Header;
            else r.decks[seat] += raw + "\n";
            continue;
        }
        if (line.empty()) continue;
        try {
            if (section == InActions) {
                r.actions.push_back(parse_action(line));
                continue;
            }
            const auto w = words(line);
            if (w[0] == "seed" && w.size() == 2 && number(w[1], r.seed)) have_seed = true;
            else if (line == "vanilla") r.vanilla = true;
            else if (w[0] == "deck" && w.size() == 2 && number(w[1], seat) && seat >= 0 && seat < player_count) {
                section = InDeck;
                have_deck[seat] = true;
            } else if (line == "actions") section = InActions;
            else throw std::invalid_argument("expected seed, vanilla, deck <seat> or actions");
        } catch (const std::exception& e) {
            throw ReplayError(where + e.what());
        }
    }
    if (section == InDeck) throw ReplayError("deck " + std::to_string(seat) + " without \"end\"");
    if (!have_seed || !have_deck[0] || !have_deck[1]) throw ReplayError("needs seed, deck 0 and deck 1");
    return r;
}

ReplayResult run_replay(const Replay& replay, const CardDb& db,
                        const std::function<void(const GameState&, const Action&)>& each) {
    const Deck seat0 = parse_deck(db, replay.decks[0]), seat1 = parse_deck(db, replay.decks[1]);
    ReplayResult result;
    std::string last = "setup";
    try {
        result.state = new_game(seat0, seat1, {.seed = replay.seed, .vanilla = replay.vanilla});
        GameChecks checks(result.state);
        if (auto p = checks.check(result.state); !p.empty()) throw std::logic_error(p);
        for (const Action& a : replay.actions) {
            last = describe(result.state, a);
            if (each) each(result.state, a);
            apply(result.state, a);
            ++result.applied;
            if (auto p = checks.check(result.state); !p.empty()) throw std::logic_error(p);
        }
    } catch (const std::exception& e) {
        result.problem = "turn " + std::to_string(result.state.turn) + ", action " + std::to_string(result.applied) +
                         " (" + last + "): " + e.what();
    }
    return result;
}

std::string to_text(const Replay& replay, const CardDb& db) {
    std::string out = "# buddy-game --replay <file>\nseed " + std::to_string(replay.seed) + "\n";
    if (replay.vanilla) out += "vanilla\n";
    for (int p = 0; p < player_count; ++p) {
        out += "deck " + std::to_string(p) + "\n" + replay.decks[p];
        if (!replay.decks[p].empty() && replay.decks[p].back() != '\n') out += "\n";
        out += "end\n";
    }
    out += "actions\n";
    std::size_t i = 0;
    const ReplayResult result = run_replay(replay, db, [&](const GameState& s, const Action& a) {
        std::string line = format_action(a);
        line.resize(std::max<std::size_t>(line.size() + 1, 16), ' ');
        out += line + "# turn " + std::to_string(s.turn) + ", player " + std::to_string(s.decision.player) + ": " +
               describe(s, a) + "\n";
        ++i;
    });
    for (; i < replay.actions.size(); ++i) out += format_action(replay.actions[i]) + "\n";
    if (!result.problem.empty()) out += "# " + result.problem + "\n";
    return out;
}

} // namespace buddy::riftbound
