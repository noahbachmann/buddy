// Random-vs-random games in vanilla mode: invariants after every action, every game ends, spells and reactions get
// played (#22's and #23's "Done when"), units move, fight and score, and most games end on points from scoring
// (#25's): a 40-card deck burns out after turn 70 at the earliest. apply() accepts exactly legal_actions() (#36: it
// checks one action without building the list).
#include <gtest/gtest.h>

#include <algorithm>

#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

void play_many(const Deck& a, const Deck& b, int games) {
    std::array<int, player_count> wins{};
    int spells = 0, reactions = 0, combats = 0, showdowns = 0, early = 0;
    for (int seed = 0; seed < games; ++seed) {
        const RandomGame g = play_random(a, b, static_cast<std::uint64_t>(seed));
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        ASSERT_TRUE(g.winner);
        EXPECT_GE(g.points[*g.winner], victory_score);
        ++wins[*g.winner];
        spells += g.spells;
        reactions += g.reactions;
        combats += g.combats;
        showdowns += g.showdowns;
        early += g.turns < 60;
    }
    EXPECT_GT(spells, games);
    EXPECT_GT(reactions, 0);
    EXPECT_GT(combats, games);
    EXPECT_GT(showdowns, combats);
    EXPECT_GT(early, games * 9 / 10);
    EXPECT_GT(wins[0], 0);
    EXPECT_GT(wins[1], 0);
}

} // namespace

TEST(RiftboundRandom, SimpleDecks) {
    play_many(test::spell_deck("A"), test::spell_deck("B"), 50);
}

TEST(RiftboundRandom, RealDecksVanilla) {
    play_many(load_deck(fixture(), kDir + "/decks/vi-unleashed.txt"),
              load_deck(fixture(), kDir + "/decks/zed-vendetta.txt"), 50);
}

TEST(RiftboundRandom, SameSeedSameGame) {
    const Deck a = test::spell_deck("A"), b = test::spell_deck("B");
    const RandomGame x = play_random(a, b, 77), y = play_random(a, b, 77);
    EXPECT_EQ(x.actions, y.actions);
    EXPECT_EQ(x.winner, y.winner);
    EXPECT_EQ(x.points, y.points);
    EXPECT_EQ(x.reactions, y.reactions);
}

TEST(RiftboundRandom, ReportsAGameThatDoesNotEnd) {
    const RandomGame g = play_random(test::simple_deck("A"), test::simple_deck("B"), 1, 10);
    EXPECT_NE(g.problem.find("no winner yet"), std::string::npos) << g.problem;
}

// Along rules-on games of the real decks: at every Priority, every Play/Activate/Move/Pass/EndTurn that could be
// asked for (and wrong obj/n fields) is accepted by apply() exactly when legal_actions() offers it.
TEST(RiftboundRandom, ApplyAcceptsExactlyTheLegalActions) {
    const Deck a = load_deck(fixture(), kDir + "/decks/vi-unleashed.txt");
    const Deck b = load_deck(fixture(), kDir + "/decks/zed-vendetta.txt");
    int accepted = 0, refused = 0;
    for (std::uint64_t seed = 0; seed < 3; ++seed) {
        const RandomGame g = play_random(a, b, seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << g.problem;
        GameState s = new_game(a, b, {.seed = seed, .vanilla = false});
        for (const Action& next : g.log) {
            if (s.decision.kind == DecisionKind::Priority) {
                std::vector<Action> probes;
                for (ObjId id = 0; id < s.objects.size(); ++id) probes.push_back({ActionKind::Play, id});
                for (const ObjId id : s.player(s.decision.player).hand) probes.push_back({ActionKind::Play, id, 1});
                std::vector<ObjId> sources = s.active;
                sources.push_back(s.player(s.decision.player).hand.empty() ? 0 : s.player(s.decision.player).hand[0]);
                for (const ObjId id : sources) {
                    for (std::uint8_t n = 0; n < 3; ++n) probes.push_back({ActionKind::Activate, id, n});
                }
                for (std::uint8_t w = 0; w <= s.battlefields.size(); ++w) {
                    const auto where = w == s.battlefields.size() ? to_base : w;
                    const ObjId card = where == to_base ? no_obj : s.battlefields[w].card;
                    probes.push_back({ActionKind::Move, card, where});
                    probes.push_back({ActionKind::Move, card == no_obj ? s.battlefields[0].card : no_obj, where});
                }
                probes.push_back({ActionKind::Move, no_obj, static_cast<std::uint8_t>(s.battlefields.size())});
                for (const ActionKind k : {ActionKind::Pass, ActionKind::EndTurn, ActionKind::Done, ActionKind::Pick}) {
                    probes.push_back({k});
                    probes.push_back({k, 0, 1});
                }
                const auto legal = legal_actions(s);
                for (const Action& p : probes) {
                    const bool offered = std::find(legal.begin(), legal.end(), p) != legal.end();
                    GameState copy = s;
                    bool ok = true;
                    try {
                        apply(copy, p);
                    } catch (const IllegalAction&) {
                        ok = false;
                        EXPECT_TRUE(copy == s) << "refused, but changed the state";
                    }
                    ASSERT_EQ(ok, offered) << "seed " << seed << ", turn " << s.turn << ": " << describe(s, p);
                    ++(ok ? accepted : refused);
                }
            }
            apply(s, next);
        }
    }
    EXPECT_GT(accepted, 500);
    EXPECT_GT(refused, accepted);
}
