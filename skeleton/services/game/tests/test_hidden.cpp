// Hidden information (108, 128): who knows which card (Object::known), view(), determinize().
#include <gtest/gtest.h>

#include <set>

#include "test_cards.hpp"
#include "test_hidden.hpp"

using namespace buddy::riftbound;
using test::check_view;

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

bool hidden(const GameState& v, ObjId id) { return v.obj(id).card == hidden_card(); }

} // namespace

TEST(RiftboundHidden, ViewAtTheStart) {  // 108.4.e, 108.5.d, 108.7.c, 128.4, 128.5
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    for (PlayerId p = 0; p < player_count; ++p) {
        const GameState v = view(s, p);
        const Player &me = v.player(p), &opp = v.player(opponent(p));
        for (const ObjId id : me.hand) EXPECT_FALSE(hidden(v, id));
        for (const ObjId id : opp.hand) EXPECT_TRUE(hidden(v, id));
        for (const Player* pl : {&me, &opp}) {
            for (const ObjId id : pl->main_deck) EXPECT_TRUE(hidden(v, id));
            for (const ObjId id : pl->rune_deck) EXPECT_TRUE(hidden(v, id));
            EXPECT_FALSE(hidden(v, pl->legend.front()));
            EXPECT_FALSE(hidden(v, pl->champion.front()));
        }
        for (const Battlefield& b : v.battlefields) EXPECT_FALSE(hidden(v, b.card));
        EXPECT_EQ(v.rng, Rng{});  // the next shuffle is secret too
    }
}

TEST(RiftboundHidden, IdsSayNothingAboutCards) {  // setup creates deck objects in shuffled order
    std::set<const Card*> first;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const GameState s = new_game(vi(), zed(), {.seed = seed, .vanilla = true});
        first.insert(s.obj(s.player(0).legend.front() + 2).card);  // legend, champion, then the deck's first object
    }
    EXPECT_GT(first.size(), 3u);
}

TEST(RiftboundHidden, ACardSeenStaysKnown) {  // 128.4: returned to the hand, the opponent still knows it
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId id = s.player(0).main_deck.back();
    move_to(s, id, Zone::Base, 0);
    move_to(s, id, Zone::Hand, 0);
    EXPECT_FALSE(hidden(view(s, 1), id));
    move_to(s, id, Zone::MainDeck, 0, 0, Pos::Bottom);
    EXPECT_FALSE(hidden(view(s, 1), id));  // its place in the deck is known too
}

TEST(RiftboundHidden, RecyclingSeveralForgetsWhichIsWhich) {  // 417.5
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const auto hand = s.player(0).hand;
    recycle(s, {hand[0]});
    EXPECT_FALSE(hidden(view(s, 0), hand[0]));  // one card: the owner knows it's at the bottom
    recycle(s, {hand[1], hand[2]});
    EXPECT_TRUE(hidden(view(s, 0), hand[1]));
    EXPECT_TRUE(hidden(view(s, 0), hand[2]));
}

TEST(RiftboundHidden, DeterminizeKeepsTheViewAndShufflesTheRest) {
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    Rng rng(9);
    EXPECT_EQ(check_view(s, vi(), zed(), rng, true), "");
    const GameState v = view(s, 0);
    const GameState a = determinize(v, vi(), zed(), rng), b = determinize(v, vi(), zed(), rng);
    EXPECT_NE(a.player(1).hand, std::vector<ObjId>{});
    EXPECT_FALSE(a == b);  // different samples
    for (const ObjId id : s.player(0).hand) EXPECT_EQ(a.obj(id).card, s.obj(id).card);
}

TEST(RiftboundHidden, DeterminizeRefusesAViewOfOtherDecks) {
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    Rng rng(1);
    EXPECT_THROW(determinize(view(s, 0), zed(), vi(), rng), std::logic_error);
}

TEST(RiftboundHidden, ViewsAlongRandomGames) {  // real decks with and without rules, spell decks; both seats' views
    for (std::uint64_t seed = 0; seed < 6; ++seed) {
        EXPECT_EQ(test::views_along_a_game(vi(), zed(), seed, true), "");
        EXPECT_EQ(test::views_along_a_game(vi(), zed(), seed, false), "");
    }
    const Deck a = test::spell_deck("A"), b = test::spell_deck("B");
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        EXPECT_EQ(test::views_along_a_game(a, b, seed, true), "");
    }
}

// 5.6: identities revealed from outside (table scans, Dev's real cards) bound into the state.

TEST(RiftboundHidden, BindSwapsWithAnUnknownCardOfTheOwner) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const auto& deck = s.player(0).main_deck;
    const ObjId top = deck.back();
    const auto other = std::find_if(deck.begin(), deck.end(), [&](ObjId id) { return s.obj(id).card != s.obj(top).card; });
    ASSERT_NE(other, deck.end());
    const ObjId partner = *other;
    const Card *was = s.obj(top).card, *want = s.obj(partner).card;
    const GameState before = s;
    ASSERT_TRUE(bind(s, top, want, 0));  // Buddy (seat 0) scans its top card: it is `want`
    EXPECT_EQ(s.obj(top).card, want);
    EXPECT_NE(std::count_if(s.objects.begin(), s.objects.end(), [&](const Object& o) { return o.card == was; }), 0);
    EXPECT_EQ(test::card_counts(s), test::card_counts(before));
    for (ObjId id = 0; id < s.objects.size(); ++id) {  // zones, gen and known stay
        EXPECT_EQ(s.obj(id).zone, before.obj(id).zone);
        EXPECT_EQ(s.obj(id).gen, before.obj(id).gen);
        EXPECT_EQ(s.obj(id).known, before.obj(id).known);
    }
    EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
    EXPECT_EQ(view(s, 0), view(before, 0));
    EXPECT_EQ(view(s, 1), view(before, 1));
    EXPECT_TRUE(bind(s, top, want, 0));  // already that card
}

TEST(RiftboundHidden, BindRefusesWhatCantBe) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const GameState before = s;
    const ObjId top = s.player(0).main_deck.back(), rune = s.player(0).rune_deck.back();
    EXPECT_FALSE(bind(s, top, s.obj(rune).card, 0));             // a rune into the main deck
    EXPECT_FALSE(bind(s, rune, s.obj(top).card, 0));             // and back
    EXPECT_FALSE(bind(s, top, s.obj(s.player(1).main_deck.back()).card, 0));  // the opponent's card
    EXPECT_FALSE(bind(s, s.player(0).legend.front(), s.obj(top).card, 0));    // public
    EXPECT_EQ(s, before);
    // Buddy's own hand is known to Buddy: never in Buddy's pool. A card only in it can't be bound to the deck.
    std::map<const Card*, int> in_deck;
    for (const ObjId id : s.player(0).main_deck) ++in_deck[s.obj(id).card];
    for (const ObjId id : s.player(0).hand) {
        if (in_deck.count(s.obj(id).card)) continue;
        EXPECT_FALSE(bind(s, top, s.obj(id).card, 0));
        EXPECT_EQ(s, before);
    }
}

TEST(RiftboundHidden, RebindCorrectsAMisreadHandCard) {  // a misread at draw: the hand object takes the true card
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId held = s.player(0).hand.front();
    const auto& deck = s.player(0).main_deck;
    const auto other = std::find_if(deck.begin(), deck.end(), [&](ObjId id) { return s.obj(id).card != s.obj(held).card; });
    ASSERT_NE(other, deck.end());
    const Card *misread = s.obj(held).card, *truth = s.obj(*other).card;
    const GameState before = s;
    ASSERT_TRUE(bind(s, held, truth, 0));
    EXPECT_EQ(s.obj(held).card, truth);
    EXPECT_EQ(s.obj(*other).card, misread);  // the misread card is still somewhere in the unknown deck
    EXPECT_EQ(test::card_counts(s), test::card_counts(before));
    EXPECT_EQ(view(s, 1), view(before, 1));  // Dev saw nothing
    EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
}

// #50: the opening hand is the engine's guess until the table deals it. A card the guess put on another hand object
// (no copy left in the deck) swaps from there; never from a kept (dealt) one, a deck object, or after Setup.
TEST(RiftboundHidden, BindOpeningHandSwapsWithinTheHand) {
    for (std::uint64_t seed = 1; seed < 50; ++seed) {
        GameState s = new_game(vi(), zed(), {.seed = seed, .vanilla = true});
        const auto hand = s.player(0).hand;
        std::map<const Card*, int> in_deck;
        for (const ObjId id : s.player(0).main_deck) ++in_deck[s.obj(id).card];
        const auto only = std::find_if(hand.begin() + 1, hand.end(), [&](ObjId id) {  // the one copy left
            const Card* c = s.obj(id).card;
            return !in_deck.count(c) && std::count_if(hand.begin(), hand.end(), [&](ObjId h) { return s.obj(h).card == c; }) == 1;
        });
        if (only == hand.end()) continue;
        const Card *was = s.obj(hand[0]).card, *want = s.obj(*only).card;
        const GameState before = s;
        const std::vector<ObjId> kept{*only};
        EXPECT_FALSE(buddy::riftbound::bind(s, hand[0], want, 0, kept));  // dealt already: keeps its card
        EXPECT_FALSE(buddy::riftbound::bind(s, s.player(0).main_deck.back(), want, 0));  // a deck object never takes a hand card
        EXPECT_EQ(s, before);
        ASSERT_TRUE(buddy::riftbound::bind(s, hand[0], want, 0));
        EXPECT_EQ(s.obj(hand[0]).card, want);
        EXPECT_EQ(s.obj(*only).card, was);
        EXPECT_EQ(view(s, 1), view(before, 1));  // the opponent sees no difference
        EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
        BindMemory memory;  // a bound hand card is kept, so the next bind can't take it back
        memory.add(s, hand[0]);
        EXPECT_FALSE(buddy::riftbound::bind(s, *only, want, 0, memory.keep(s, 0)));
        while (s.phase == Phase::Setup) apply(s, legal_actions(s).back());  // both keep (Done): turn 1
        const GameState started = s;
        EXPECT_FALSE(buddy::riftbound::bind(s, *only, want, 0));  // the same swap after Setup: refused
        EXPECT_EQ(s, started);
        return;
    }
    FAIL() << "no seed with a hand card that has no copy in the deck";
}

TEST(RiftboundHidden, DevsHandIsInTheirPool) {  // Dev plays real cards: their hand is as unknown to Buddy as their deck
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId placeholder = s.player(1).hand.front();
    const ObjId in_deck = s.player(1).main_deck.front();
    const Card* played = s.obj(in_deck).card;
    const GameState before = s;
    ASSERT_TRUE(bind(s, placeholder, played, 0));
    EXPECT_EQ(s.obj(placeholder).card, played);
    EXPECT_EQ(view(s, 0), view(before, 0));  // Buddy's view of Dev's hidden cards: unchanged
    // and from Dev's hand: binding a deck card to a card only in Dev's hand works too
    const auto& hand = s.player(1).hand;
    ASSERT_GT(hand.size(), 1u);
    EXPECT_TRUE(bind(s, s.player(1).main_deck.back(), s.obj(hand[1]).card, 0));
}

// Two copies dealt in one step are both bound on the state before it, where both still sit in the deck: the second
// bind must not take the first one's card (keep), else the first object ends up holding some other card.
TEST(RiftboundHidden, BindKeepsTheGroupsEarlierBinds) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const auto& deck = s.player(0).main_deck;
    std::map<const Card*, std::vector<ObjId>> by_card;
    for (const ObjId id : deck) by_card[s.obj(id).card].push_back(id);
    const auto two = std::find_if(by_card.begin(), by_card.end(), [](const auto& kv) { return kv.second.size() >= 2; });
    ASSERT_NE(two, by_card.end());
    const Card* x = two->first;
    std::vector<ObjId> others;  // deck objects that don't hold x
    for (const ObjId id : deck) {
        if (s.obj(id).card != x) others.push_back(id);
    }
    ASSERT_GE(others.size(), 3u);
    const ObjId a = others[0], b = others[1];
    ASSERT_TRUE(bind(s, a, x, 0));
    const std::vector<ObjId> keep{a};
    ASSERT_TRUE(buddy::riftbound::bind(s, b, x, 0, keep));
    EXPECT_EQ(s.obj(a).card, x);
    EXPECT_EQ(s.obj(b).card, x);
    EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
    // and with every holder of x kept, there's no partner: refused, nothing changes
    std::vector<ObjId> all{a, b};
    for (const ObjId id : two->second) all.push_back(id);
    const GameState before = s;
    EXPECT_FALSE(buddy::riftbound::bind(s, others[2], x, 0, all));
    EXPECT_EQ(s, before);
}

// #49: a bind keeps its card while the object sits where it was bound and the knower doesn't know it; a move (gen)
// or the knower learning it ends that.
TEST(RiftboundHidden, BindMemoryKeepsUntilMovedOrKnown) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    BindMemory memory;
    const ObjId hand = s.player(1).hand.front(), deck = s.player(1).main_deck.front(), mine = s.player(0).main_deck.back();
    for (const ObjId id : {hand, deck, mine}) memory.add(s, id);
    EXPECT_EQ(memory.keep(s, 0), (std::vector<ObjId>{hand, deck, mine}));
    s.obj(mine).known |= 1u;  // Buddy (seat 0) learns its card
    move_to(s, deck, Zone::MainDeck, 1, 0, Pos::Bottom);  // moved: a new object (124)
    EXPECT_EQ(memory.keep(s, 0), std::vector<ObjId>{hand});
    move_to(s, hand, Zone::Trash, 1);  // public
    EXPECT_EQ(memory.keep(s, 0), std::vector<ObjId>{});
}

// #49: Dev's battlefield is the engine's random pick until Dev names theirs: it swaps with a removed one (487.5).
TEST(RiftboundHidden, BindABattlefield) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId field = s.battlefields[1].card;
    ASSERT_EQ(s.obj(field).owner, 1);
    std::vector<ObjId> removed;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        if (s.obj(id).owner == 1 && s.obj(id).zone == Zone::Removed) removed.push_back(id);
    }
    ASSERT_EQ(removed.size(), 2u);
    const Card* was = s.obj(field).card;
    const Card* want = s.obj(removed[1]).card;
    ASSERT_TRUE(bind(s, field, want, 0));
    EXPECT_EQ(s.obj(field).card, want);
    EXPECT_EQ(s.obj(removed[1]).card, was);
    EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
    EXPECT_FALSE(bind(s, field, s.obj(s.battlefields[0].card).card, 0));  // Buddy's: not one of Dev's
    EXPECT_FALSE(bind(s, field, s.obj(s.player(1).main_deck.back()).card, 0));  // not a battlefield
}

TEST(RiftboundHidden, SetHandOrder) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    auto hand = s.player(0).hand;
    std::reverse(hand.begin(), hand.end());
    EXPECT_TRUE(set_hand_order(s, 0, hand));
    EXPECT_EQ(s.player(0).hand, hand);
    EXPECT_FALSE(set_hand_order(s, 0, {hand[0], hand[0], hand[1], hand[2]}));
    EXPECT_FALSE(set_hand_order(s, 0, {hand.begin(), hand.end() - 1}));
    EXPECT_FALSE(set_hand_order(s, 0, s.player(1).hand));
    EXPECT_EQ(s.player(0).hand, hand);
    EXPECT_EQ(check_invariants(s), std::vector<std::string>{});
}

TEST(RiftboundHidden, PublicViewShowsOnlyWhatEveryoneKnows) {
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId top = s.player(0).main_deck.back();
    s.obj(top).known |= 1u;  // Buddy predicted it (Vision)
    const GameState v = public_view(s);
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        EXPECT_EQ(hidden(v, id), s.obj(id).known != everyone) << id;
    }
    for (PlayerId p = 0; p < player_count; ++p) {
        for (const ObjId id : s.player(p).hand) EXPECT_TRUE(hidden(v, id));
    }
    EXPECT_TRUE(hidden(v, top));
    EXPECT_FALSE(hidden(v, s.player(0).legend.front()));
    EXPECT_EQ(v.rng, Rng{});
}

TEST(RiftboundHidden, RandomBindsAlongRandomGames) {  // either seat as Buddy, with and without rules
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        for (PlayerId buddy = 0; buddy < player_count; ++buddy) {
            EXPECT_EQ(test::binds_along_a_game(vi(), zed(), seed, true, buddy), "");
            EXPECT_EQ(test::binds_along_a_game(zed(), vi(), seed, false, buddy), "");
        }
    }
}
