// Setup (Core Rules 110–117) and Duel's 487.5, the mulligan as a decision.
#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "riftbound/engine.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

const Deck& vi() {
    static const Deck d = load_deck(fixture(), kDir + "/decks/vi-unleashed.txt");
    return d;
}

const Deck& zed() {
    static const Deck d = load_deck(fixture(), kDir + "/decks/zed-vendetta.txt");
    return d;
}

GameState simple_game(std::uint64_t seed) {
    static const Deck a = test::simple_deck("A"), b = test::simple_deck("B");
    return new_game(a, b, {.seed = seed, .vanilla = true});
}

GameState real_game(std::uint64_t seed) { return new_game(vi(), zed(), {.seed = seed, .vanilla = true}); }

std::vector<std::string> names(const GameState& s, const std::vector<ObjId>& ids) {
    std::vector<std::string> out;
    for (const ObjId id : ids) out.push_back(s.obj(id).card->name);
    return out;
}

void keep(GameState& s) { apply(s, {ActionKind::Done}); }

} // namespace

TEST(RiftboundSetup, ZonesAfterSetup) {  // 111–116, 487.5
    const GameState s = real_game(1);
    EXPECT_TRUE(check_invariants(s).empty());
    for (PlayerId p = 0; p < player_count; ++p) {
        const Player& pl = s.player(p);
        const Deck& deck = p == 0 ? vi() : zed();
        EXPECT_EQ(names(s, pl.legend), std::vector{deck.legend->name});
        EXPECT_EQ(names(s, pl.champion), std::vector{deck.champion->name});
        EXPECT_EQ(pl.hand.size(), 4u);
        EXPECT_EQ(pl.main_deck.size(), 40u - 1 - 4);  // chosen champion in its zone, 4 drawn
        EXPECT_EQ(pl.rune_deck.size(), 12u);
        EXPECT_TRUE(pl.base.empty());
        EXPECT_EQ(pl.points, 0);
    }
    ASSERT_EQ(s.battlefields.size(), 2u);  // 487.4: one per player
    EXPECT_EQ(s.obj(s.battlefields[0].card).owner, 0);
    EXPECT_EQ(s.obj(s.battlefields[1].card).owner, 1);
    int removed = 0;
    for (const Object& o : s.objects) removed += o.zone == Zone::Removed ? 1 : 0;
    EXPECT_EQ(removed, 4);  // two unused battlefields each
    EXPECT_FALSE(s.battlefields[0].controller);
    EXPECT_EQ(s.turn, 0);
    EXPECT_EQ(s.phase, Phase::Setup);
    EXPECT_EQ(s.turn_player, s.first_player);
    EXPECT_EQ(s.decision.kind, DecisionKind::Mulligan);  // 117: the first player first
    EXPECT_EQ(s.decision.player, s.first_player);
}

TEST(RiftboundSetup, ConservesEveryCard) {  // each of the 2 × 56 cards in exactly one zone
    const GameState s = real_game(2);
    EXPECT_EQ(s.objects.size(), 2u * 56);
    for (PlayerId p = 0; p < player_count; ++p) {
        const Deck& deck = p == 0 ? vi() : zed();
        std::multiset<std::string> want{deck.legend->name, deck.champion->name};
        for (const auto* entries : {&deck.main, &deck.runes}) {
            for (const auto& e : *entries) {
                for (int i = 0; i < e.count; ++i) want.insert(e.card->name);
            }
        }
        for (const Card* b : deck.battlefields) want.insert(b->name);
        std::multiset<std::string> have;
        for (const Object& o : s.objects) {
            if (o.owner == p) have.insert(o.card->name);
        }
        EXPECT_EQ(have, want);
    }
}

TEST(RiftboundSetup, SameSeedSameGame) {
    for (const std::uint64_t seed : {0ull, 1ull, 42ull, 0xffffffffffffffffull}) {
        GameState a = real_game(seed), b = real_game(seed);
        EXPECT_EQ(a, b) << seed;
        const Action pick = legal_actions(a).front();
        for (GameState* s : {&a, &b}) {
            apply(*s, pick);
            keep(*s);
            keep(*s);
        }
        EXPECT_EQ(a, b) << seed;
    }
}

TEST(RiftboundSetup, SeedDecidesOrderBattlefieldsAndFirstPlayer) {  // 114, 115, 487.5
    std::set<int> first;
    std::set<std::string> fields;
    std::set<std::vector<ObjId>> decks;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        const GameState s = real_game(seed);
        first.insert(s.first_player);
        fields.insert(s.obj(s.battlefields[0].card).card->name);
        decks.insert(s.player(0).main_deck);
    }
    EXPECT_EQ(first.size(), 2u);
    EXPECT_EQ(fields.size(), 3u);
    EXPECT_EQ(decks.size(), 32u);
}

TEST(RiftboundSetup, RealDecksNeedTheirCards) {  // D21: unimplemented = hard error, runes are built in
    try {
        new_game(vi(), zed(), {.seed = 1});
        FAIL() << "no error";
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("Zed, Master of Shadows"), std::string::npos) << what;
        EXPECT_NE(what.find("Zaun Warrens"), std::string::npos) << what;
        EXPECT_EQ(what.find("Fury Rune"), std::string::npos) << what;
    }
}

TEST(RiftboundMulligan, KeepOrSetAsideUpToTwo) {  // 117
    GameState s = simple_game(7);
    const PlayerId first = s.first_player;
    const auto hand = s.player(first).hand;

    // picks go in hand order: after the 2nd card only the 3rd and 4th are left; after two picks only Done
    std::vector<Action> want{{ActionKind::Pick, hand[0]}, {ActionKind::Pick, hand[1]}, {ActionKind::Pick, hand[2]},
                             {ActionKind::Pick, hand[3]}, {ActionKind::Done}};
    EXPECT_EQ(legal_actions(s), want);
    EXPECT_EQ(describe(s, want[4]), "keep");
    apply(s, {ActionKind::Pick, hand[1]});
    EXPECT_EQ(legal_actions(s), (std::vector<Action>{{ActionKind::Pick, hand[2]}, {ActionKind::Pick, hand[3]},
                                                     {ActionKind::Done}}));
    EXPECT_THROW(apply(s, {ActionKind::Pick, hand[0]}), IllegalAction);
    apply(s, {ActionKind::Pick, hand[3]});
    EXPECT_EQ(legal_actions(s), std::vector<Action>{{ActionKind::Done}});
    EXPECT_EQ(describe(s, {ActionKind::Done}), "done");

    const auto top = s.player(first).main_deck;  // back = top: the next two draws
    const GameState before = s;
    EXPECT_THROW(apply(s, {ActionKind::Pick, hand[2]}), IllegalAction);
    EXPECT_EQ(s, before);  // a refused action changes nothing
    apply(s, {ActionKind::Done});

    const Player& pl = s.player(first);
    std::set<ObjId> now(pl.hand.begin(), pl.hand.end());
    EXPECT_EQ(now, (std::set<ObjId>{hand[0], hand[2], top[top.size() - 1], top[top.size() - 2]}));
    // 117.3 + 417: the set-aside cards at the bottom of the deck, as new objects
    EXPECT_EQ((std::set<ObjId>{pl.main_deck[0], pl.main_deck[1]}), (std::set<ObjId>{hand[1], hand[3]}));
    EXPECT_EQ(pl.main_deck.size(), 35u);
    EXPECT_EQ(s.obj(hand[1]).gen, 3);  // hand → set aside → deck; drawing made it 1
    EXPECT_TRUE(check_invariants(s).empty());

    // then the other player (117: in turn order), then setup is over: turns follow with #22
    EXPECT_EQ(s.decision.kind, DecisionKind::Mulligan);
    EXPECT_EQ(s.decision.player, opponent(first));
    const auto other = s.player(opponent(first)).hand;
    keep(s);
    EXPECT_EQ(s.player(opponent(first)).hand, other);
    EXPECT_EQ(s.decision.kind, DecisionKind::None);
    EXPECT_TRUE(legal_actions(s).empty());
    EXPECT_THROW(keep(s), IllegalAction);
}

TEST(RiftboundMulligan, RecycledOrderIsRandom) {  // 417.5
    std::set<bool> first_at_bottom;
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        GameState s = simple_game(seed);
        const PlayerId first = s.first_player;
        const auto hand = s.player(first).hand;
        apply(s, {ActionKind::Pick, hand[0]});
        apply(s, {ActionKind::Pick, hand[1]});
        keep(s);
        const auto& deck = s.player(first).main_deck;
        first_at_bottom.insert(deck[0] == hand[0]);
    }
    EXPECT_EQ(first_at_bottom.size(), 2u);
}

TEST(RiftboundMulligan, RandomChoicesKeepInvariants) {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        GameState s = seed % 2 ? real_game(seed) : simple_game(seed);
        Rng pick(seed);
        for (int steps = 0; s.decision.kind != DecisionKind::None; ++steps) {
            ASSERT_LT(steps, 20);
            const auto actions = legal_actions(s);
            ASSERT_FALSE(actions.empty());
            apply(s, actions[pick.below(actions.size())]);
            const auto problems = check_invariants(s);
            ASSERT_TRUE(problems.empty()) << seed << ": " << problems.front();
        }
        for (PlayerId p = 0; p < player_count; ++p) {
            EXPECT_EQ(s.player(p).hand.size(), 4u);
            EXPECT_EQ(s.player(p).main_deck.size(), 35u);
        }
    }
}
