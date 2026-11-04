// Engine moves → table commands (5.5, #42): random and greedy games over the real decks, both decks from both seats.
// After every action the commands for Buddy's seat run on a fault-free sim (through table's StandIn, as the soak
// does), and the sim's truth must be the engine's zones: hand order, deck and rune order, every spot with exhausted.
// Coverage counters make sure the games went through draws, Burn, channel, Vision, mulligan, Burn Out, recycle,
// return to hand, discards, tokens and exhaust/ready; Burn Out and the mulligan also get a targeted test.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "riftbound/ai/greedy.hpp"
#include "riftbound/ai/player.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"
#include "soak.hpp"
#include "table_moves.hpp"

using namespace buddy;
using namespace buddy::riftbound;
using game::TableMove;
using json = nlohmann::json;
using table::CommandKind;
using TPlace = table::Place;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& db() {
    static const CardDb d = CardDb::load(kDir + "/tests/data/cards.json");
    return d;
}

Deck deck(const std::string& name) {
    std::ifstream in(kDir + "/decks/" + name + ".txt");
    std::stringstream text;
    text << in.rdbuf();
    return parse_deck(db(), text.str());
}

std::optional<TPlace> spot_of(const Object& o) {
    switch (o.zone) {
        case Zone::Base: return TPlace::Base;
        case Zone::Battlefield: return o.where == 0 ? TPlace::Bf0 : TPlace::Bf1;
        case Zone::Facedown: return o.where == 0 ? TPlace::Fd0 : TPlace::Fd1;
        case Zone::Chain: return TPlace::Chain;
        case Zone::Trash: return TPlace::Trash;
        case Zone::Banishment: return TPlace::Banish;
        case Zone::ChampionZone: return TPlace::Champion;
        case Zone::LegendZone: return TPlace::Legend;
        default: return std::nullopt;
    }
}

// "" if the sim's truth is the engine's zones for `seat`.
std::string compare(const GameState& s, PlayerId seat, const table::StandIn& t) {
    const auto codes = [&](const std::vector<ObjId>& ids) {
        std::vector<std::string> out;
        for (const ObjId id : ids) out.push_back(game::code_of(s, id));
        return out;
    };
    const auto truth = [](const std::vector<table::Phys>& pile) {
        std::vector<std::string> out;
        for (const auto& c : pile) out.push_back(c.code);
        return out;
    };
    const Player& pl = s.player(seat);
    const auto& sim = t.sim();
    if (codes(pl.hand) != truth(sim.pile(t.table().tracker().hand_pile()))) {
        std::string e = "hand differs: engine", t2 = ", table";
        for (const auto& c : codes(pl.hand)) e += " " + c;
        for (const auto& c : truth(sim.pile(t.table().tracker().hand_pile()))) t2 += " " + c;
        for (const ObjId id : pl.hand) e += " [" + std::to_string(id) + "→" + std::to_string(t.id_of(id)) + "]";
        for (const auto& k : t.table().tracker().belief().hand) t2 += " [" + std::to_string(k.id) + "]";
        return e + t2 + " recoveries " + std::to_string(t.table().tracker().counts().recoveries) + " feeds " + std::to_string(t.table().tracker().counts().feeds);
    }
    if (codes(pl.main_deck) != truth(sim.pile(table::Pile::Deck))) return "deck order differs";
    if (codes(pl.rune_deck) != truth(sim.pile(table::Pile::Runes))) return "rune deck order differs";
    std::map<TPlace, std::vector<std::pair<std::string, bool>>> engine, tab;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.owner != seat || o.card->type == CardType::Battlefield) continue;
        if (o.zone == Zone::Removed) {
            if (!o.card->token) return "a card set aside";
            continue;
        }
        if (const auto p = spot_of(o)) engine[*p].push_back({game::code_of(s, id), o.exhausted});
    }
    for (int p = 0; p < table::place_count; ++p) {
        for (const auto& c : sim.spot(static_cast<TPlace>(p))) tab[static_cast<TPlace>(p)].push_back({c.code, c.exhausted});
    }
    for (auto* m : {&engine, &tab}) {
        for (auto& [p, list] : *m) std::sort(list.begin(), list.end());
        std::erase_if(*m, [](const auto& kv) { return kv.second.empty(); });
    }
    for (const auto& [p, list] : engine) {
        if (tab[p] != list) return std::string(table::to_string(p)) + " differs";
    }
    if (engine.size() != tab.size()) return "a spot holds cards the engine doesn't";
    if (const auto belief = sim.compare(t.table().tracker().belief(), t.table().tracker().hand_pile()); !belief.empty()) {
        return "belief: " + belief;
    }
    return {};
}

struct Coverage {
    std::map<std::string, int> seen;
    void count(const TableMove& m) {
        const auto& c = m.cmd;
        std::string k(table::to_string(c.kind));
        if (c.kind == CommandKind::Deal) k += std::string(" ") + std::string(table::to_string(c.from)) + "→" + std::string(table::to_string(c.to));
        if (c.kind == CommandKind::Play || c.kind == CommandKind::Move) k += "→" + std::string(table::to_string(c.to));
        if (c.kind == CommandKind::Exhaust) k += c.exhausted ? " exhaust" : " ready";
        ++seen[k];
    }
};

// One referee game as table commands for `seat`, checked after every action.
class Replay {
public:
    Replay(const Deck& d0, const Deck& d1, PlayerId seat, std::uint64_t seed) : seat_(seat), moves_(seat) {
        std::vector<LoggedMove> log;
        {
            game::MoveRecorder rec;
            s = new_game(d0, d1, {.seed = seed});
            log = rec.take();
        }
        const game::TableSetup t = game::table_setup(s, log, seat);
        json setup = {{"legend", game::code_of(s, t.legend)}, {"champion", game::code_of(s, t.champion)},
                      {"deck", json::array()}, {"runes", json::array()},
                      {"objs", {{"legend", t.legend}, {"champion", t.champion}, {"deck", t.deck}, {"runes", t.runes}}}};
        for (const ObjId id : t.deck) setup["deck"].push_back(game::code_of(s, id));
        for (const ObjId id : t.runes) setup["runes"].push_back(game::code_of(s, id));
        stand_in_ = std::make_unique<table::StandIn>(setup, table::SoakOptions{}, seed);
        sync(log, "setup");
    }

    // Applies an action (or a raw state change in `edit`) with the move log on, runs Buddy's commands, compares.
    void act(const Action& a) {
        std::vector<LoggedMove> log;
        const std::string what = describe(s, a);
        {
            game::MoveRecorder rec;
            apply(s, a);
            log = rec.take();
        }
        sync(log, what);
    }
    template <class F>
    void edit(F&& f, const std::string& what) {
        std::vector<LoggedMove> log;
        {
            game::MoveRecorder rec;
            f(s);
            log = rec.take();
        }
        sync(log, what);
    }

    GameState s;
    Coverage coverage;
    std::string problem;

private:
    void sync(const std::vector<LoggedMove>& log, const std::string& what) {
        if (!problem.empty()) return;
        std::vector<TableMove> cmds;
        try {
            cmds = moves_.step(s, log);
        } catch (const std::exception& e) {
            problem = "turn " + std::to_string(s.turn) + " (" + what + "): " + e.what();
            return;
        }
        for (const TableMove& m : cmds) {
            coverage.count(m);
            stand_in_->apply({{"cmd", table::to_json(m.cmd)}, {"objs", m.objs}});
        }
        if (const auto& u = stand_in_->unrecovered_so_far(); !u.empty()) {
            problem = "turn " + std::to_string(s.turn) + " (" + what + "): " + u.front();
            return;
        }
        if (auto diff = compare(s, seat_, *stand_in_); !diff.empty()) {
            problem = "turn " + std::to_string(s.turn) + " (" + what + "): " + diff;
        }
    }
    PlayerId seat_;
    game::TableMoves moves_;
    std::unique_ptr<table::StandIn> stand_in_;
};

Coverage play_games(int games, bool greedy) {
    const Deck vi = deck("vi-unleashed"), zed = deck("zed-vendetta");
    Coverage total;
    for (int i = 0; i < games; ++i) {
        const bool swapped = (i / 2) % 2 == 1;
        const auto seat = static_cast<PlayerId>(i % 2);
        const auto seed = static_cast<std::uint64_t>(i);
        Replay r(swapped ? zed : vi, swapped ? vi : zed, seat, seed);
        std::array<std::unique_ptr<ai::Player>, player_count> players;
        for (PlayerId p = 0; p < player_count; ++p) {
            if (greedy) players[p] = std::make_unique<ai::GreedyPlayer>(seed * 2 + p);
            else players[p] = std::make_unique<ai::RandomPlayer>(seed * 2 + p);
            players[p]->begin(p, swapped ? zed : vi, swapped ? vi : zed);
        }
        while (!r.s.winner && r.problem.empty()) {
            const auto actions = legal_actions(r.s);
            const PlayerId p = r.s.decision.player;
            const Action a = actions.size() == 1 ? actions[0] : players[p]->choose(view(r.s, p), actions);
            r.act(p == seat ? game::nearest_copy(r.s, a) : a);
        }
        EXPECT_EQ(r.problem, "") << (greedy ? "greedy" : "random") << " game " << i << ", seat " << int(seat);
        for (const auto& [k, n] : r.coverage.seen) total.seen[k] += n;
    }
    return total;
}


int seen(const Coverage& c, const std::string& k) { return c.seen.count(k) ? c.seen.at(k) : 0; }

} // namespace

TEST(RiftboundTableMoves, RandomGamesReplayOnTheSim) {
    const Coverage c = play_games(40, false);
    // draws, Burn, channel, plays, spells, discards, mulligan, return to hand, deaths, recycle from the trash, rune
    // payment, Vision, tokens, exhaust/ready
    for (const char* k : {"deal deck→hand", "deal deck→trash", "deal runes→base", "play→base", "play→chain",
                          "play→trash", "play→deck_bottom", "move→hand", "move→trash", "move→deck_bottom",
                          "move→runes_bottom", "move→bf0", "move→bf1", "move→banish", "peek", "peek_done", "token",
                          "remove", "exhaust exhaust", "exhaust ready"}) {
        EXPECT_GT(seen(c, k), 0) << k;
    }
}

TEST(RiftboundTableMoves, GreedyGamesReplayOnTheSim) { play_games(8, true); }

TEST(RiftboundTableMoves, Mulligan) {
    Replay r(deck("vi-unleashed"), deck("zed-vendetta"), 0, 3);
    while (r.s.decision.kind == DecisionKind::Mulligan && r.s.decision.player != 0) r.act({ActionKind::Done});
    ASSERT_EQ(r.s.decision.kind, DecisionKind::Mulligan);
    const auto hand = r.s.player(0).hand;
    r.act({ActionKind::Pick, hand[1]});
    r.act({ActionKind::Pick, hand[2]});
    if (r.s.decision.kind == DecisionKind::Mulligan) r.act({ActionKind::Done});
    EXPECT_EQ(r.problem, "");
    EXPECT_EQ(seen(r.coverage, "play→deck_bottom"), 2);
    EXPECT_EQ(seen(r.coverage, "deal deck→hand"), 2);  // 4 at setup + 2 replacements: two deal commands
    const auto& d = r.s.player(0).main_deck;  // the two set aside lie under the deck, in the engine's random order
    EXPECT_EQ(std::set<ObjId>({d[0], d[1]}), std::set<ObjId>({hand[1], hand[2]}));
}

TEST(RiftboundTableMoves, BurnOut) {
    Replay r(deck("vi-unleashed"), deck("zed-vendetta"), 1, 5);
    r.edit([](GameState& s) { burn(s, 1, static_cast<int>(s.player(1).main_deck.size())); }, "burn the deck");
    ASSERT_EQ(r.s.player(1).main_deck.size(), 0u);
    const int points = r.s.player(0).points;
    Rng rng(9);
    while (r.problem.empty() && !r.s.winner && r.s.player(0).points == points) {
        const auto actions = legal_actions(r.s);
        r.act(actions[rng.below(actions.size())]);
    }
    EXPECT_EQ(r.problem, "");
    EXPECT_GT(r.s.player(0).points, points);  // the opponent scored the Burn Out
    EXPECT_GT(seen(r.coverage, "move→deck_bottom"), 30);  // the whole trash went back under the deck
    for (int i = 0; i < 200 && r.problem.empty() && !r.s.winner; ++i) {  // and the game goes on from the new deck
        const auto actions = legal_actions(r.s);
        r.act(actions[rng.below(actions.size())]);
    }
    EXPECT_EQ(r.problem, "");
}

// 5.5's gate in small (#43): 200 random-player games' workload against the sim under the stress rates and the
// defaults, nothing unrecovered. The full gate (10 000 greedy games) runs from the command line: plan → "5.5 as built".
TEST(RiftboundTableMoves, SoakUnderStress) {
    const Deck vi = deck("vi-unleashed"), zed = deck("zed-vendetta");
    std::stringstream lines;
    for (int i = 0; i < 200; ++i) {
        const bool swapped = (i / 2) % 2 == 1;
        const auto w = game::workload_game(swapped ? zed : vi, swapped ? vi : zed, static_cast<PlayerId>(i % 2),
                                           static_cast<std::uint64_t>(i), false);
        ASSERT_EQ(w.problem, "") << "game " << i;
        for (const auto& l : w.lines) lines << l.dump() << '\n';
    }
    table::SoakOptions o;
    o.faults = table::parse_faults("stress");
    std::stringstream log;
    const table::SoakReport r = table::soak(lines, o, &log);
    EXPECT_EQ(r.games, 200);
    EXPECT_EQ(r.unrecovered, 0) << log.str();
    long faults = 0, corrections = 0;
    for (const auto& g : r.per_game) {
        faults += g.faults.total();
        corrections += g.corrections;
    }
    EXPECT_GT(faults, 1000);      // the faults happened
    EXPECT_GT(corrections, 20);   // and some needed a correction
}
