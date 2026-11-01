// The AI (5.4, riftbound/ai/): action keys, the arena (players see only views, matches are reproducible), eval and
// v1 greedy on hand-built states.
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>

#include "riftbound/ai/arena.hpp"
#include "riftbound/ai/greedy.hpp"
#include "riftbound/hidden.hpp"
#include "test_real.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;
using namespace buddy::riftbound::ai;

namespace {

const Card* kTwo = make("Two", CardType::Unit, 1, 2, plain_rules);
const Card* kFive = make("Five", CardType::Unit, 5, 5, plain_rules);

Deck frame() { return deck_of("A", {kTwo, kFive, zap()}); }

// Turn 1's main phase for the first player, both hands back in the decks.
GameState empty_handed() {
    static const Deck d = frame();
    GameState s = started(d);
    for (PlayerId p = 0; p < player_count; ++p) {
        while (!s.player(p).hand.empty()) move_to(s, s.player(p).hand.back(), Zone::MainDeck, p);
    }
    return s;
}

Action move_to_field(const GameState& s, std::uint8_t b) { return {ActionKind::Move, s.battlefields[b].card, b}; }

// The real decks on the fixture's cards (rules attached).
const std::array<Deck, 2>& real_decks() {
    static const std::array<Deck, 2> decks = [] {
        const std::string dir = std::string(BUDDY_SOURCE_DIR) + "/services/game/decks/";
        std::array<Deck, 2> d;
        int i = 0;
        for (const char* name : {"vi-unleashed", "zed-vendetta"}) {
            std::ifstream in(dir + name + ".txt");
            std::stringstream text;
            text << in.rdbuf();
            d[i++] = parse_deck(fixture(), text.str());
        }
        return d;
    }();
    return decks;
}

// Random, but checks every state it gets: a view of its own seat (viewing it again changes nothing, RNG reset).
class SpyPlayer : public RandomPlayer {
public:
    using RandomPlayer::RandomPlayer;
    void begin(PlayerId seat, const Deck& a, const Deck& b) override {
        seat_ = seat;
        RandomPlayer::begin(seat, a, b);
    }
    Action choose(const GameState& v, const std::vector<Action>& actions) override {
        ++asked;
        not_views += !(view(v, seat_) == v) || v.decision.player != seat_;
        for (const ObjId id : v.player(opponent(seat_)).hand) hidden += v.obj(id).card == hidden_card();
        return RandomPlayer::choose(v, actions);
    }
    int asked = 0, not_views = 0, hidden = 0;

private:
    PlayerId seat_ = 0;
};

} // namespace

TEST(RiftboundAi, KeysMergeCopiesInHandNotOnTheBoard) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player;
    const ObjId a = put(s, p, zap(), Zone::Hand), b = put(s, p, zap(), Zone::Hand);
    const ObjId u = put(s, p, kTwo, Zone::Base), w = put(s, p, kTwo, Zone::Base);
    EXPECT_EQ(key_of(s, {ActionKind::Play, a}), key_of(s, {ActionKind::Play, b}));
    EXPECT_NE(key_of(s, {ActionKind::Pick, u}), key_of(s, {ActionKind::Pick, w}));
    EXPECT_NE(key_of(s, {ActionKind::Play, a}), key_of(s, {ActionKind::Pick, a}));
    const auto actions = distinct(s, legal_actions(s));
    EXPECT_EQ(std::count_if(actions.begin(), actions.end(), [](const Action& x) { return x.kind == ActionKind::Play; }),
              1);
}

TEST(RiftboundAi, WilsonAndPercentile) {
    const auto [low, high] = wilson(180, 200);
    EXPECT_NEAR(low, 0.851, 0.001);
    EXPECT_NEAR(high, 0.934, 0.001);
    EXPECT_GT(wilson(114, 200)[0], 0.5);  // the plan's 115 / 200 has one game to spare
    EXPECT_LT(wilson(113, 200)[0], 0.5);
    EXPECT_EQ(percentile({3, 1, 2, 4}, 50), 2);
    EXPECT_EQ(percentile({3, 1, 2, 4}, 100), 4);
    EXPECT_EQ(percentile({}, 95), 0);
}

TEST(RiftboundAi, PairingsCoverBothSeatsAndDecks) {
    std::set<std::pair<bool, bool>> seen;
    for (int i = 0; i < 4; ++i) seen.insert({pairing(7, i).b_in_seat0, pairing(7, i).decks_swapped});
    EXPECT_EQ(seen.size(), 4u);
    EXPECT_EQ(pairing(7, 0).seed, pairing(7, 1).seed);  // a seed with the players swapped
    EXPECT_EQ(pairing(7, 3).seed, 8u);
}

TEST(RiftboundAi, PlayersOnlyGetViews) {
    const auto& d = real_decks();
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        SpyPlayer a(seed), b(seed + 100);
        const ArenaGame g = play_game(d[0], d[1], {&a, &b}, seed);
        ASSERT_TRUE(g.problem.empty()) << g.problem;
        EXPECT_GT(a.asked, 0);
        EXPECT_EQ(a.not_views + b.not_views, 0);
        EXPECT_GT(a.hidden + b.hidden, 0);  // the opponent's hand, seen as hidden cards
        int decisions = 0, forced = 0;
        for (const SeatStats& st : g.seats) {
            decisions += static_cast<int>(st.legal.size());
            forced += st.forced;
            for (const int n : st.legal) EXPECT_GE(n, 1);
        }
        EXPECT_EQ(decisions, g.actions);
        EXPECT_EQ(decisions - forced, a.asked + b.asked);  // forced moves never asked
    }
}

TEST(RiftboundAi, MatchesAreReproducibleAcrossJobs) {
    const auto& d = real_decks();
    const PlayerFactory random = [](std::uint64_t seed) { return std::make_unique<RandomPlayer>(seed); };
    const auto one = play_match(random, random, d[0], d[1], 12, 3, 1);
    const auto three = play_match(random, random, d[0], d[1], 12, 3, 3);
    for (std::size_t i = 0; i < one.size(); ++i) {
        ASSERT_TRUE(one[i].game.problem.empty()) << one[i].game.problem;
        EXPECT_EQ(one[i].game.log, three[i].game.log) << i;
        EXPECT_TRUE(one[i].winning_player() && one[i].winning_deck());
    }
}

TEST(RiftboundAi, EvalCountsPointsAndBoard) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player, o = opponent(p);
    const double start = eval(s, p);
    EXPECT_DOUBLE_EQ(start, -eval(s, o));
    s.player(p).points = 1;
    EXPECT_GT(eval(s, p), start);
    const double one = eval(s, p);
    put(s, p, kTwo, Zone::Base);
    EXPECT_GT(eval(s, p), one);
    s.player(p).points = 7;
    s.player(o).points = 6;
    EXPECT_GT(eval(s, p) - eval(s, p, {.near_win = 0}), 0);  // the last points weigh more
    s.winner = o;
    EXPECT_EQ(eval(s, p), -std::numeric_limits<double>::infinity());
}

TEST(RiftboundAi, MulliganSetsAsideExpensiveCards) {
    GameState s = new_game(frame(), frame(), {.seed = 5});
    const PlayerId p = s.decision.player;
    auto& hand = s.player(p).hand;
    for (const ObjId id : hand) s.obj(id).card = kTwo;
    EXPECT_EQ(mulligan_choice(s, legal_actions(s), 5).kind, ActionKind::Done);
    s.obj(hand[2]).card = kFive;
    EXPECT_EQ(greedy_choice(s), (Action{ActionKind::Pick, hand[2]}));
}

TEST(RiftboundAi, GreedyPrefersTheConquer) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player, o = opponent(p);
    at_field(s, o, kFive, 1);
    ready_in_base(s, p, kTwo);
    EXPECT_EQ(greedy_choice(s), move_to_field(s, 0));
}

TEST(RiftboundAi, GreedyTakesLethal) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player, o = opponent(p);
    s.player(p).points = victory_score - 1;
    s.player(p).scored = 0b10;  // battlefield 1 this turn: conquering 0 is the final point (473.1.b)
    ready_in_base(s, p, kTwo);
    put(s, o, kFive, Zone::Base);
    put(s, p, zap(), Zone::Hand);  // killing the Five would look good too
    EXPECT_EQ(greedy_choice(s), move_to_field(s, 0));
    for (int n = 0; n < 50 && !s.winner; ++n) apply(s, greedy_choice(s));
    EXPECT_EQ(s.winner, p);
}

TEST(RiftboundAi, GreedyDoesntTradeDown) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player, o = opponent(p);
    at_field(s, o, kFive, 0);
    at_field(s, o, kFive, 1);
    ready_in_base(s, p, kTwo);
    ASSERT_TRUE(offered(s, move_to_field(s, 0)));
    EXPECT_EQ(greedy_choice(s).kind, ActionKind::EndTurn);
}

TEST(RiftboundAi, GreedyDoesntHitItsOwnUnit) {
    GameState s = empty_handed();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId mine = put(s, p, kFive, Zone::Base), theirs = put(s, o, kFive, Zone::Base);
    s.obj(mine).exhausted = true;  // no move to conquer instead
    const ObjId z = put(s, p, zap(), Zone::Hand);
    EXPECT_EQ(greedy_choice(s), (Action{ActionKind::Play, z}));
    play(s, z);
    ASSERT_EQ(s.decision.purpose, Purpose::Target);
    ASSERT_TRUE(offered(s, {ActionKind::Pick, mine}));
    EXPECT_EQ(greedy_choice(s), (Action{ActionKind::Pick, theirs}));
}

TEST(RiftboundAi, GreedyBeatsRandom) {
    const auto& d = real_decks();
    const PlayerFactory greedy = [](std::uint64_t seed) { return std::make_unique<GreedyPlayer>(seed); };
    const PlayerFactory random = [](std::uint64_t seed) { return std::make_unique<RandomPlayer>(seed); };
    const auto games = play_match(greedy, random, d[0], d[1], 8, 0, 4);
    int wins = 0;
    for (const auto& g : games) {
        ASSERT_TRUE(g.game.problem.empty()) << g.game.problem;
        wins += g.winning_player() == 0;
    }
    EXPECT_GE(wins, 7);
}
