// The chain and priority (327–340), playing cards (349–359), timing (307–310, Action 806, Reaction 813), "do as much as
// you can" (055) and cleanups (318–323).
#include <gtest/gtest.h>

#include <algorithm>

#include "riftbound/engine.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const Card* const kSpell = test::spell("Plain Spell", 0);
const Card* const kAction = test::spell("Action Spell", 0, Timing::Action);
const Card* const kReaction = test::spell("Reaction Spell", 0, Timing::Reaction);
const Card* const kPricey = test::spell("Pricey Reaction", 1, Timing::Reaction);
const Card* const kQuickUnit = [] {
    Card c;
    c.name = "Quick Unit";
    c.domains = domain::fury;
    c.energy = 0;
    c.might = 2;
    c.timing = Timing::Reaction;
    return test::card(std::move(c));
}();
const Card* const kGear = [] {
    Card c;
    c.name = "Some Gear";
    c.type = CardType::Gear;
    c.domains = domain::fury;
    c.energy = 0;
    return test::card(std::move(c));
}();

// Both keep: turn 1's priority. Every special card in both decks.
GameState started() {
    static const Deck d = [] {
        Deck deck = test::simple_deck("P");
        deck.main[0] = {kSpell, 3};
        deck.main[1] = {kAction, 3};
        deck.main[2] = {kReaction, 3};
        deck.main[3] = {kPricey, 3};
        deck.main[4] = {kQuickUnit, 3};
        deck.main[5] = {kGear, 3};
        return deck;
    }();
    GameState s = new_game(d, d, {.seed = 3, .vanilla = true});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

ObjId to_hand(GameState& s, PlayerId p, const Card* card) {
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.card == card && o.owner == p && o.zone != Zone::Hand) {
            move_to(s, id, Zone::Hand, p);
            return id;
        }
    }
    throw std::logic_error("no copy left");
}

bool offered(const GameState& s, const Action& a) {
    const auto actions = legal_actions(s);
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

void play(GameState& s, ObjId id) { apply(s, {ActionKind::Play, id}); }
void pass(GameState& s) { apply(s, {ActionKind::Pass}); }

void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

} // namespace

TEST(RiftboundChain, SpellResolvesAfterBothPass) {  // 337.4, 338.1.b, 339.1, 340.2, 359.3
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId spell = to_hand(s, p, kSpell);
    EXPECT_EQ(turn_state(s), TurnState::NeutralOpen);
    play(s, spell);
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_TRUE(s.chain[0].finalized);
    EXPECT_EQ(s.obj(spell).zone, Zone::Chain);
    EXPECT_EQ(turn_state(s), TurnState::NeutralClosed);
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    EXPECT_EQ(s.decision.player, p);  // finalizing doesn't pass priority (337.1.a)
    EXPECT_FALSE(offered(s, {ActionKind::EndTurn}));
    EXPECT_EQ(legal_actions(s).back(), Action{ActionKind::Pass});
    expect_ok(s);

    pass(s);
    EXPECT_EQ(s.decision.player, o);
    EXPECT_EQ(s.obj(spell).zone, Zone::Chain);
    expect_ok(s);
    pass(s);
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(s.obj(spell).zone, Zone::Trash);
    EXPECT_EQ(s.decision.player, p);  // 335: open again, the turn player's main phase
    EXPECT_EQ(legal_actions(s).back(), Action{ActionKind::EndTurn});
    expect_ok(s);
}

TEST(RiftboundChain, NothingToDoStillResolves) {  // 055.1: no possible instruction, still played and resolved
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId spell = to_hand(s, p, kAction);
    const auto hand = s.player(p).hand.size();
    play(s, spell);
    pass(s);
    pass(s);
    EXPECT_EQ(s.obj(spell).zone, Zone::Trash);
    EXPECT_EQ(s.player(p).hand.size(), hand - 1);
}

TEST(RiftboundChain, NewestResolvesFirstThenPriorityToItsController) {  // 340.1, 340.4
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId first = to_hand(s, p, kSpell);
    const ObjId answer = to_hand(s, o, kReaction);
    play(s, first);
    pass(s);
    ASSERT_EQ(s.decision.player, o);
    play(s, answer);
    ASSERT_EQ(s.chain.size(), 2u);
    EXPECT_EQ(s.decision.player, o);  // 337.4: the newest item's controller
    EXPECT_EQ(s.passes, 0);           // adding an item restarts the passes (339.1)
    pass(s);
    EXPECT_EQ(s.decision.player, p);
    pass(s);
    EXPECT_EQ(s.obj(answer).zone, Zone::Trash);  // newest first
    EXPECT_EQ(s.obj(first).zone, Zone::Chain);
    EXPECT_EQ(s.decision.player, p);  // 340.4: controller of the newest item left
    expect_ok(s);
    pass(s);
    EXPECT_EQ(s.decision.player, o);
    pass(s);
    EXPECT_EQ(s.obj(first).zone, Zone::Trash);
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(s.decision.player, p);
    expect_ok(s);
}

TEST(RiftboundChain, ReactToOwnSpell) {  // 338.1.a.5: the first priority after creating the chain can add more
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId spell = to_hand(s, p, kSpell);
    const ObjId reaction = to_hand(s, p, kReaction);
    play(s, spell);
    EXPECT_TRUE(offered(s, {ActionKind::Play, reaction}));
    play(s, reaction);
    pass(s);
    pass(s);
    EXPECT_EQ(s.obj(reaction).zone, Zone::Trash);
    EXPECT_EQ(s.obj(spell).zone, Zone::Chain);
}

TEST(RiftboundChain, ClosedStateTakesOnlyReactions) {  // 309.1.a, 331.1, 813.1.c.1
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId spell = to_hand(s, p, kSpell);
    const ObjId plain = to_hand(s, o, kSpell), action = to_hand(s, o, kAction);
    const ObjId reaction = to_hand(s, o, kReaction), quick = to_hand(s, o, kQuickUnit);
    const ObjId gear = to_hand(s, o, kGear);
    play(s, spell);
    pass(s);
    ASSERT_EQ(s.decision.player, o);
    EXPECT_FALSE(offered(s, {ActionKind::Play, plain}));
    EXPECT_FALSE(offered(s, {ActionKind::Play, action}));
    EXPECT_FALSE(offered(s, {ActionKind::Play, gear}));
    EXPECT_TRUE(offered(s, {ActionKind::Play, reaction}));
    EXPECT_TRUE(offered(s, {ActionKind::Play, quick}));
    const GameState before = s;
    EXPECT_THROW(play(s, action), IllegalAction);
    EXPECT_EQ(s, before);  // refused, untouched
}

TEST(RiftboundChain, UnplayableReactionWithoutRunes) {  // payable only
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);  // o: no runes yet on turn 1
    const ObjId pricey = to_hand(s, o, kPricey);
    play(s, to_hand(s, p, kSpell));
    pass(s);
    EXPECT_FALSE(offered(s, {ActionKind::Play, pricey}));
    channel(s, o, 1);
    s.cleanup_due = false;
    EXPECT_TRUE(offered(s, {ActionKind::Play, pricey}));
    play(s, pricey);  // paid on the opponent's turn
    EXPECT_EQ(s.obj(pricey).zone, Zone::Chain);
    EXPECT_TRUE(s.obj(s.player(o).base.front()).exhausted);
    expect_ok(s);
}

TEST(RiftboundChain, PermanentOnTheChainResolvesAtOnce) {  // 337.2, 359.2
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId spell = to_hand(s, p, kSpell);
    const ObjId quick = to_hand(s, o, kQuickUnit);
    play(s, spell);
    pass(s);
    play(s, quick);
    EXPECT_EQ(s.obj(quick).zone, Zone::Base);
    EXPECT_TRUE(s.obj(quick).exhausted);
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.decision.player, p);  // the spell's controller: newest item left on the chain
    EXPECT_EQ(s.passes, 0);
    expect_ok(s);
}

TEST(RiftboundChain, ShowdownTiming) {  // 308.1.a, 310, 806.1.c.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId plain = to_hand(s, p, kSpell), action = to_hand(s, p, kAction), reaction = to_hand(s, p, kReaction);
    const ObjId unit = to_hand(s, p, kGear);
    EXPECT_TRUE(playable(s, p, plain));
    EXPECT_TRUE(playable(s, p, unit));
    s.showdown = true;  // set by hand until #25 starts showdowns
    EXPECT_EQ(turn_state(s), TurnState::ShowdownOpen);
    EXPECT_FALSE(playable(s, p, plain));
    EXPECT_FALSE(playable(s, p, unit));
    EXPECT_TRUE(playable(s, p, action));
    EXPECT_TRUE(playable(s, p, reaction));
    s.chain.push_back({});
    EXPECT_EQ(turn_state(s), TurnState::ShowdownClosed);
    EXPECT_FALSE(playable(s, p, action));
    EXPECT_TRUE(playable(s, p, reaction));
}

TEST(RiftboundChain, RunesAndLegendsAreNotPlayed) {
    GameState s = started();
    const PlayerId p = s.first_player;
    EXPECT_FALSE(playable(s, p, s.player(p).legend.front()));
    EXPECT_FALSE(playable(s, p, s.player(p).base.front()));
}

TEST(RiftboundCleanup, LethalDamageKills) {  // 142.4.b, 323.4, 322
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId mine = to_hand(s, p, kQuickUnit), theirs = to_hand(s, o, kQuickUnit);
    move_to(s, mine, Zone::Base, p);
    move_to(s, theirs, Zone::Base, o);
    s.cleanup_due = false;
    s.obj(mine).damage = 1;    // 2 Might: survives
    s.obj(theirs).damage = 5;  // more than enough
    play(s, to_hand(s, p, kSpell));  // any cleanup does it
    EXPECT_EQ(s.obj(mine).zone, Zone::Base);
    EXPECT_EQ(s.obj(theirs).zone, Zone::Trash);
    EXPECT_EQ(s.obj(theirs).damage, 0);  // a new object (124)

    s.obj(mine).buffs = -1;  // 1 Might now: the same damage is lethal at the next cleanup
    pass(s);
    pass(s);  // the spell leaves the chain (319.5)
    EXPECT_EQ(s.obj(mine).zone, Zone::Trash);
    expect_ok(s);
}

TEST(RiftboundCleanup, ZeroDamageNeverLethal) {  // 142.4.b: non-zero
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId unit = to_hand(s, p, kQuickUnit);
    move_to(s, unit, Zone::Base, p);
    s.obj(unit).buffs = -2;  // 0 Might
    EXPECT_FALSE(lethal_damage(s, unit));
    s.obj(unit).damage = 1;
    EXPECT_TRUE(lethal_damage(s, unit));
}

TEST(RiftboundCleanup, BoardStateSteps) {  // 323.5, 323.6, 323.6.a
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId gear = to_hand(s, p, kGear), unit = to_hand(s, p, kQuickUnit), hidden = to_hand(s, o, kReaction);
    move_to(s, gear, Zone::Battlefield, p, 0);
    move_to(s, unit, Zone::Battlefield, p, 1);
    move_to(s, hidden, Zone::Facedown, o, 1);
    s.battlefields[0].controller = p;  // no unit of p's there
    s.battlefields[1].controller = p;  // p's unit holds it
    play(s, to_hand(s, p, kSpell));
    pass(s);
    pass(s);  // resolved: cleanups in the open state
    EXPECT_FALSE(s.battlefields[0].controller);
    EXPECT_EQ(s.battlefields[1].controller, p);
    EXPECT_EQ(s.obj(gear).zone, Zone::Base);     // recalled
    EXPECT_EQ(s.obj(unit).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(hidden).zone, Zone::Trash);  // o's hidden card at p's battlefield
    expect_ok(s);
}

TEST(RiftboundCleanup, ControlStaysWhileClosed) {  // 323.5: open state only
    GameState s = started();
    const PlayerId p = s.first_player;
    play(s, to_hand(s, p, kSpell));
    s.battlefields[0].controller = p;
    s.cleanup_due = true;
    play(s, to_hand(s, p, kReaction));  // a cleanup in the closed state
    EXPECT_EQ(s.battlefields[0].controller, p);
    pass(s);
    pass(s);  // the reaction resolves, the chain still exists
    EXPECT_EQ(s.battlefields[0].controller, p);
    pass(s);
    pass(s);  // open again
    EXPECT_FALSE(s.battlefields[0].controller);
}
