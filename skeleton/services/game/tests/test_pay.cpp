// Costs and paying them (131, 164.2, 357, 445) with the payment rule in pay.hpp; playing permanents (359.2).
#include <gtest/gtest.h>

#include <algorithm>

#include "riftbound/engine.hpp"
#include "riftbound/pay.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const Card* with_cost(std::string name, CardType type, Domains domains, int energy, int power) {
    Card c;
    c.name = std::move(name);
    c.type = type;
    c.domains = domains;
    c.energy = energy;
    c.power = power;
    if (type == CardType::Unit) c.might = 2;
    return test::card(std::move(c));
}

const Card* const kFuryUnit = with_cost("Fury Unit", CardType::Unit, domain::fury, 3, 1);
const Card* const kDualUnit = with_cost("Dual Unit", CardType::Unit, domain::fury | domain::chaos, 1, 1);
const Card* const kRainbow = with_cost("Rainbow Unit", CardType::Unit, 0, 0, 2);
const Card* const kGear = with_cost("Some Gear", CardType::Gear, domain::fury, 2, 0);
const Card* const kSpell = with_cost("Some Spell", CardType::Spell, domain::fury, 0, 0);

// 6 Fury and 6 Chaos runes; the special cards in the main deck.
GameState started() {
    static const Deck d = [] {
        Deck deck = test::simple_deck("P");
        deck.main[0] = {kFuryUnit, 3};
        deck.main[1] = {kDualUnit, 3};
        deck.main[2] = {kRainbow, 3};
        deck.main[3] = {kGear, 3};
        deck.main[4] = {kSpell, 3};
        deck.runes = {{test::card("Fury Rune", CardType::Rune, domain::fury), 6},
                      {test::card("Chaos Rune", CardType::Rune, domain::chaos), 6}};
        return deck;
    }();
    GameState s = new_game(d, d, {.seed = 9, .vanilla = true});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

Domains domain_of(const GameState& s, ObjId id) { return s.obj(id).card->domains; }

// p's base runes: exactly `fury` Fury and `chaos` Chaos ones, all ready.
void set_runes(GameState& s, PlayerId p, int fury, int chaos) {
    for (const ObjId id : runes_of(s, p)) move_to(s, id, Zone::RuneDeck, p);
    for (const auto& [dom, n] : {std::pair{domain::fury, fury}, std::pair{domain::chaos, chaos}}) {
        int left = n;
        const auto deck = s.player(p).rune_deck;
        for (const ObjId id : deck) {
            if (left > 0 && domain_of(s, id) == dom) {
                move_to(s, id, Zone::Base, p);
                --left;
            }
        }
        ASSERT_EQ(left, 0);
    }
    s.cleanup_due = false;  // test setup, not a game action
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

int count_exhausted(const GameState& s, PlayerId p) {
    int n = 0;
    for (const ObjId id : runes_of(s, p)) n += s.obj(id).exhausted ? 1 : 0;
    return n;
}

void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

} // namespace

TEST(RiftboundCost, Printed) {  // 131
    EXPECT_EQ(printed_cost(*kFuryUnit), (Cost{3, 1, domain::fury}));
    EXPECT_EQ(printed_cost(*test::unit("Free", 0, 1)), (Cost{0, 0, domain::fury}));
    Card no_stats;
    EXPECT_EQ(printed_cost(no_stats), (Cost{0, 0, 0}));
}

TEST(RiftboundPay, OfferedOnlyWhenPayable) {
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId unit = to_hand(s, p, kFuryUnit);
    set_runes(s, p, 0, 3);  // energy yes, Fury Power no
    EXPECT_FALSE(offered(s, {ActionKind::Play, unit}));
    set_runes(s, p, 1, 1);  // Power yes, 3 Energy no
    EXPECT_FALSE(offered(s, {ActionKind::Play, unit}));
    set_runes(s, p, 1, 2);  // the Fury rune gives [1] and then its Power
    EXPECT_TRUE(offered(s, {ActionKind::Play, unit}));
    EXPECT_EQ(legal_actions(s).back(), Action{ActionKind::EndTurn});
}

TEST(RiftboundPay, SpellsWaitForTheChain) {  // #23
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId spell = to_hand(s, p, kSpell);
    EXPECT_FALSE(offered(s, {ActionKind::Play, spell}));
}

TEST(RiftboundPay, UnitEntersExhaustedRunesPaid) {  // 359.2.c, 164.2
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId unit = to_hand(s, p, kFuryUnit);
    set_runes(s, p, 2, 2);
    const auto deck_before = s.player(p).rune_deck.size();
    apply(s, {ActionKind::Play, unit});  // one Fury rune: domain is no choice
    EXPECT_EQ(s.obj(unit).zone, Zone::Base);
    EXPECT_TRUE(s.obj(unit).exhausted);
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(runes_of(s, p).size(), 3u);  // one recycled …
    EXPECT_EQ(s.player(p).rune_deck.size(), deck_before + 1);
    EXPECT_EQ(domain_of(s, s.player(p).rune_deck.front()), domain::fury);  // … to the bottom
    EXPECT_EQ(count_exhausted(s, p), 2);  // 3 Energy, one of it from the recycled rune
    EXPECT_EQ(s.player(p).pool, Pool{});
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    expect_ok(s);
}

TEST(RiftboundPay, GearEntersReady) {  // 359.2.d
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = to_hand(s, p, kGear);
    set_runes(s, p, 1, 1);
    apply(s, {ActionKind::Play, gear});
    EXPECT_EQ(s.obj(gear).zone, Zone::Base);
    EXPECT_FALSE(s.obj(gear).exhausted);
    EXPECT_EQ(count_exhausted(s, p), 2);
    expect_ok(s);
}

TEST(RiftboundPay, RecycleExhaustedRunesFirst) {
    GameState s = started();
    const PlayerId p = s.first_player;
    set_runes(s, p, 2, 0);
    const auto fury = runes_of(s, p);
    s.obj(fury[1]).exhausted = true;
    const Cost cost{0, 1, domain::fury};
    EXPECT_EQ(recycle_options(s, p, cost, {}), std::vector{fury[1]});
    pay(s, p, cost, {fury[1]});
    EXPECT_EQ(runes_of(s, p), std::vector{fury[0]});
    EXPECT_FALSE(s.obj(fury[0]).exhausted);
}

TEST(RiftboundPay, PoolFirst) {  // 165: the pool empties anyway
    GameState s = started();
    const PlayerId p = s.first_player;
    set_runes(s, p, 1, 1);
    s.player(p).pool.energy = 2;
    s.player(p).pool.universal = 1;
    const Cost cost{3, 1, domain::fury};
    EXPECT_EQ(runes_to_recycle(s, p, cost), 0);
    EXPECT_TRUE(can_pay(s, p, cost));
    pay(s, p, cost, {});
    EXPECT_EQ(s.player(p).pool, Pool{});
    EXPECT_EQ(count_exhausted(s, p), 1);
    EXPECT_EQ(runes_of(s, p).size(), 2u);

    s.player(p).pool.power[domain_index(domain::chaos)] = 1;  // wrong domain: doesn't pay Fury
    EXPECT_EQ(runes_to_recycle(s, p, {0, 1, domain::fury}), 1);
}

TEST(RiftboundPay, DomainIsADecision) {  // two matching domains: the player picks, one rune per domain
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId unit = to_hand(s, p, kDualUnit);
    set_runes(s, p, 2, 2);
    apply(s, {ActionKind::Play, unit});
    EXPECT_EQ(s.decision.kind, DecisionKind::Recycle);
    EXPECT_EQ(s.obj(unit).zone, Zone::Chain);
    expect_ok(s);
    const auto options = legal_actions(s);
    ASSERT_EQ(options.size(), 2u);
    EXPECT_EQ(domain_of(s, options[0].obj), domain::fury);
    EXPECT_EQ(domain_of(s, options[1].obj), domain::chaos);
    EXPECT_EQ(describe(s, options[1]), "recycle Chaos Rune");
    apply(s, options[1]);
    EXPECT_EQ(s.obj(unit).zone, Zone::Base);
    EXPECT_EQ(s.obj(options[1].obj).zone, Zone::RuneDeck);
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    expect_ok(s);
}

TEST(RiftboundPay, OneDomainLeftIsNoDecision) {
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId unit = to_hand(s, p, kDualUnit);
    set_runes(s, p, 0, 2);
    apply(s, {ActionKind::Play, unit});
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    EXPECT_EQ(s.obj(unit).zone, Zone::Base);
}

TEST(RiftboundPay, RainbowPicksAreSetsNotSequences) {  // any domain, 2 symbols: {F,F} {F,C} {C,C}, each once
    GameState s = started();
    const PlayerId p = s.first_player;
    set_runes(s, p, 2, 2);
    const Cost cost = printed_cost(*kRainbow);
    const auto first = recycle_options(s, p, cost, {});
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(recycle_options(s, p, cost, {first[0]}).size(), 2u);  // Fury → Fury or Chaos
    const auto after_chaos = recycle_options(s, p, cost, {first[1]});
    ASSERT_EQ(after_chaos.size(), 1u);  // Chaos → only Chaos
    EXPECT_EQ(domain_of(s, after_chaos[0]), domain::chaos);

    set_runes(s, p, 1, 1);  // Chaos first would leave no Chaos for the 2nd pick: only Fury, then forced Chaos
    const auto forced = recycle_options(s, p, cost, {});
    ASSERT_EQ(forced.size(), 1u);
    EXPECT_EQ(domain_of(s, forced[0]), domain::fury);
}

TEST(RiftboundPay, PayChecksItsInput) {
    GameState s = started();
    const PlayerId p = s.first_player;
    set_runes(s, p, 1, 1);
    const auto r = runes_of(s, p);
    EXPECT_THROW(pay(s, p, {0, 1, domain::fury}, {}), std::logic_error);
    EXPECT_THROW(pay(s, p, {0, 1, domain::fury}, {r[1]}), std::logic_error);  // Chaos for Fury
    GameState t = s;
    EXPECT_THROW(pay(t, p, {3, 0, domain::fury}, {}), std::logic_error);
}
