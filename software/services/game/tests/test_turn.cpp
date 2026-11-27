// Turn structure (Core Rules 315–317), Burn Out (432), the win check in cleanups (194, 323.1), conceding (649).
#include <gtest/gtest.h>

#include <algorithm>

#include "riftbound/engine.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

// Both keep: stops at turn 1's priority.
GameState started(std::uint64_t seed = 5) {
    static const Deck a = test::simple_deck("A"), b = test::simple_deck("B");
    GameState s = new_game(a, b, {.seed = seed, .vanilla = true});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

void end_turn(GameState& s) { apply(s, {ActionKind::EndTurn}); }

std::size_t runes(const GameState& s, PlayerId p) {
    return static_cast<std::size_t>(std::count_if(s.player(p).base.begin(), s.player(p).base.end(),
                                                  [&](ObjId id) { return s.obj(id).card->type == CardType::Rune; }));
}

void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

// Empties p's main deck into the trash.
void mill_all(GameState& s, PlayerId p) {
    while (!s.player(p).main_deck.empty()) move_to(s, s.player(p).main_deck.back(), Zone::Trash, p);
}

} // namespace

TEST(RiftboundTurn, FirstTurns) {  // 315.3, 315.4, 487.7
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    EXPECT_EQ(s.turn, 1);
    EXPECT_EQ(s.phase, Phase::Main);
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    EXPECT_EQ(s.decision.player, first);
    EXPECT_EQ(runes(s, first), 2u);
    EXPECT_EQ(s.player(first).hand.size(), 5u);
    EXPECT_EQ(runes(s, second), 0u);
    expect_ok(s);

    end_turn(s);
    EXPECT_EQ(s.turn, 2);
    EXPECT_EQ(s.turn_player, second);
    EXPECT_EQ(s.decision.player, second);
    EXPECT_EQ(runes(s, second), 3u);  // the second player's extra rune
    EXPECT_EQ(s.player(second).hand.size(), 5u);

    end_turn(s);
    EXPECT_EQ(s.turn, 3);
    EXPECT_EQ(runes(s, first), 4u);
    end_turn(s);
    EXPECT_EQ(runes(s, second), 5u);  // only once
    expect_ok(s);
}

TEST(RiftboundTurn, ChannelWhatIsLeft) {  // 315.3.b.1
    GameState s = started();
    const PlayerId second = opponent(s.first_player);
    auto& deck = s.player(second).rune_deck;
    while (deck.size() > 1) move_to(s, deck.back(), Zone::Base, second);
    const auto before = runes(s, second);
    end_turn(s);
    EXPECT_EQ(runes(s, second), before + 1);
    EXPECT_TRUE(deck.empty());
    end_turn(s);
    end_turn(s);
    EXPECT_EQ(runes(s, second), before + 1);  // nothing left to channel, no error
    expect_ok(s);
}

TEST(RiftboundTurn, AwakenReadiesOnlyTheTurnPlayers) {  // 315.1
    GameState s = started();
    const PlayerId first = s.first_player;
    for (Object& o : s.objects) {
        if (on_board(o.zone) && o.card->type != CardType::Battlefield) o.exhausted = true;
    }
    end_turn(s);  // second's turn: their runes ready (all new), first's stay exhausted
    for (const ObjId id : s.player(first).base) EXPECT_TRUE(s.obj(id).exhausted);
    end_turn(s);
    for (const ObjId id : s.player(first).base) EXPECT_FALSE(s.obj(id).exhausted);
}

TEST(RiftboundTurn, PoolsEmpty) {  // 167: start of each main phase and end of each turn
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    s.player(first).pool.energy = 3;
    s.player(second).pool.power[0] = 1;
    end_turn(s);
    EXPECT_EQ(s.player(first).pool, Pool{});
    EXPECT_EQ(s.player(second).pool, Pool{});
}

TEST(RiftboundTurn, EndingKillsThenHeals) {  // 317.2: lethal damage kills (323.4) before units heal (3c)
    GameState s = started();
    const PlayerId first = s.first_player;
    std::vector<ObjId> units;
    for (const ObjId id : s.player(first).main_deck) {
        if (s.obj(id).card->type == CardType::Unit && s.obj(id).card->might.value_or(0) >= 2) units.push_back(id);
        if (units.size() == 2) break;
    }
    ASSERT_EQ(units.size(), 2u);
    const ObjId hurt = units[0], doomed = units[1];
    move_to(s, hurt, Zone::Base, first);
    move_to(s, doomed, Zone::Base, first);
    s.cleanup_due = false;
    s.obj(hurt).damage = static_cast<std::int16_t>(might(s, hurt) - 1);
    s.obj(doomed).damage = static_cast<std::int16_t>(might(s, doomed));
    end_turn(s);
    EXPECT_EQ(s.obj(hurt).damage, 0);
    EXPECT_EQ(s.obj(hurt).zone, Zone::Base);
    EXPECT_EQ(s.obj(doomed).zone, Zone::Trash);
    expect_ok(s);
}

TEST(RiftboundBurnOut, RecyclesTrashOpponentScoresThenDraws) {  // 432.2
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    mill_all(s, second);
    const auto trash = s.player(second).trash.size();
    end_turn(s);  // second draws from an empty deck
    EXPECT_EQ(s.player(first).points, 1);
    EXPECT_TRUE(s.player(second).trash.empty());
    EXPECT_EQ(s.player(second).main_deck.size(), trash - 1);
    EXPECT_EQ(s.player(second).hand.size(), 5u);
    EXPECT_FALSE(s.winner);
    expect_ok(s);
}

TEST(RiftboundBurnOut, EmptyTrashBurnsOutUntilTheOpponentWins) {  // 432.3, 432.3.c
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    while (!s.player(second).main_deck.empty()) {
        move_to(s, s.player(second).main_deck.back(), Zone::Banishment, second);
    }
    end_turn(s);
    EXPECT_EQ(s.winner, first);
    EXPECT_EQ(s.player(first).points, victory_score);
    EXPECT_EQ(s.decision.kind, DecisionKind::None);
    EXPECT_TRUE(legal_actions(s).empty());
    EXPECT_TRUE(check_invariants(s).empty());
}

TEST(RiftboundBurnOut, FirstBurnOutWinsOnlyInACleanup) {  // 432.3.c needs a 2nd burn out; else 323.1
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    s.player(first).points = victory_score - 1;
    mill_all(s, second);
    draw(s, second, 1);
    EXPECT_EQ(s.player(first).points, victory_score);
    EXPECT_FALSE(s.winner);  // not yet: the burn out alone doesn't end the game

    GameState g = started();
    g.player(first).points = victory_score - 1;
    mill_all(g, second);
    end_turn(g);  // the cleanup after the draw phase
    EXPECT_EQ(g.winner, first);
}

TEST(RiftboundWin, NeedsMorePointsThanTheOpponent) {  // 194.2.b
    GameState s = started();
    const PlayerId first = s.first_player, second = opponent(first);
    s.player(first).points = s.player(second).points = victory_score;
    end_turn(s);
    EXPECT_FALSE(s.winner);
    s.player(second).points = victory_score + 1;
    end_turn(s);
    EXPECT_EQ(s.winner, second);
}

TEST(RiftboundWin, Concede) {  // 650, 651.1
    GameState s = started();
    const PlayerId first = s.first_player;
    concede(s, opponent(first));  // not the deciding player: any time
    EXPECT_EQ(s.winner, first);
    EXPECT_EQ(s.decision.kind, DecisionKind::None);
    EXPECT_THROW(end_turn(s), IllegalAction);
    concede(s, first);  // game over: no change
    EXPECT_EQ(s.winner, first);
    expect_ok(s);
}
