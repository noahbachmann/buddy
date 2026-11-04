// Tracker and command layer against the fault-free sim: every command works, plays cost exactly k feeds, the hand
// order after plays and returns is the engine's (original order minus the card, returns on top).
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim.hpp"
#include "table.hpp"

using namespace buddy::table;

namespace {

// Deck bottom → top: A1 … ; the top card is dealt first.
SimSetup setup_of(std::vector<std::string> deck, std::vector<std::string> runes = {"R1", "R1", "R2"}) {
    SimSetup s;
    s.deck = std::move(deck);
    s.runes = std::move(runes);
    s.legend = "LEG";
    s.champion = "CHAMP";
    return s;
}

struct Fixture {
    explicit Fixture(SimSetup s, TrackerConfig cfg = {}) : sim(s, {}, 1), table(sim, cfg) {
        run({.kind = CommandKind::Setup, .legend = s.legend, .champion = s.champion,
             .deck = static_cast<int>(s.deck.size()), .runes = static_cast<int>(s.runes.size())});
    }

    // One command: its last event must be done; returns its cards. Checks belief = truth after it.
    std::vector<CardRef> run(Command c) {
        c.seq = ++seq;
        const auto events = table.execute(c);
        EXPECT_FALSE(events.empty());
        const Event& last = events.back();
        EXPECT_EQ(last.seq, c.seq);
        EXPECT_EQ(last.kind, EventKind::Done) << to_json(last).dump();
        EXPECT_EQ(sim.compare(table.tracker().belief(), table.tracker().hand_pile()), "");
        corrections = static_cast<int>(events.size()) - 1;
        return last.cards;
    }
    Event fail(Command c) {
        c.seq = ++seq;
        const auto events = table.execute(c);
        EXPECT_EQ(events.back().kind, EventKind::Failed);
        EXPECT_EQ(sim.compare(table.tracker().belief(), table.tracker().hand_pile()), "");
        return events.back();
    }
    std::vector<CardRef> draw(int n) { return run({.kind = CommandKind::Deal, .to = Place::Hand, .n = n}); }
    CardRef play(int id, Place to = Place::Base) { return run({.kind = CommandKind::Play, .to = to, .id = id}).at(0); }
    std::vector<std::string> hand() const {
        std::vector<std::string> out;
        for (const auto& k : table.tracker().belief().hand) out.push_back(k.code);
        return out;
    }
    const OpCounts& counts() const { return table.tracker().counts(); }

    Sim sim;
    Table table;
    std::int64_t seq = 0;
    int corrections = 0;
};

} // namespace

TEST(TableTracker, SetupGivesLegendAndChampionIds) {
    Fixture f(setup_of({"A", "B"}));
    const auto& b = f.table.tracker().belief();
    EXPECT_EQ(b.deck, 2);
    EXPECT_EQ(b.runes, 3);
    EXPECT_EQ(b.spots[static_cast<int>(Place::Legend)].at(0).code, "LEG");
    EXPECT_EQ(b.spots[static_cast<int>(Place::Champion)].at(0).code, "CHAMP");
}

TEST(TableTracker, DrawsGoOnTopOfTheHand) {
    Fixture f(setup_of({"E", "D", "C", "B", "A"}));
    const auto cards = f.draw(3);
    ASSERT_EQ(cards.size(), 3u);
    EXPECT_EQ(cards[0].code, "A");  // the top card first
    EXPECT_EQ(cards[2].code, "C");
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "B", "C"}));  // bottom → top: first drawn at the bottom
    EXPECT_EQ(f.table.tracker().belief().deck, 2);
    EXPECT_EQ(f.counts().feeds, 0);
}

TEST(TableTracker, PlayCostsExactlyItsIndexInFeeds) {
    Fixture f(setup_of({"F", "E", "D", "C", "B", "A"}));
    const auto cards = f.draw(6);  // hand A B C D E F
    long feeds = f.counts().feeds;
    EXPECT_EQ(f.play(cards[3].id).code, "D");
    EXPECT_EQ(f.counts().feeds - feeds, 3);
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "B", "C", "E", "F"}));
    feeds = f.counts().feeds;
    EXPECT_EQ(f.play(cards[0].id, Place::Trash).code, "A");
    EXPECT_EQ(f.counts().feeds - feeds, 0);
    feeds = f.counts().feeds;
    f.play(cards[5].id, Place::Chain);
    EXPECT_EQ(f.counts().feeds - feeds, 3);
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"B", "C", "E"}));
    EXPECT_EQ(f.counts().recoveries, 0);
    EXPECT_EQ(f.counts().scans, 6 * 3 + 3 * 3);  // per deal and play: 2 confirming reads + the post-check
}

TEST(TableTracker, ReturnsGoOnTopAndKeepTheOrder) {
    Fixture f(setup_of({"D", "C", "B", "A"}));
    const auto cards = f.draw(3);  // A B C
    f.play(cards[1].id);           // B to base
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "C"}));
    f.run({.kind = CommandKind::Move, .to = Place::Hand, .id = cards[1].id});
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "C", "B"}));
    f.draw(1);
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "C", "B", "D"}));
    f.play(cards[1].id);  // B again, now at index 2
    EXPECT_EQ(f.hand(), (std::vector<std::string>{"A", "C", "D"}));
}

TEST(TableTracker, SameCodeCopiesPlayExactlyTheAskedOne) {
    Fixture f(setup_of({"X", "B", "X", "A"}));
    const auto cards = f.draw(4);  // A X B X
    const long feeds = f.counts().feeds;
    EXPECT_EQ(f.play(cards[3].id).id, cards[3].id);  // the far copy: game asks for the near one when it can
    EXPECT_EQ(f.counts().feeds - feeds, 3);
    EXPECT_EQ(f.table.tracker().belief().hand[1].id, cards[1].id);
}

TEST(TableTracker, RunesBoardMovesAndExhaust) {
    Fixture f(setup_of({"A"}));
    const auto runes = f.run({.kind = CommandKind::Deal, .from = Source::Runes, .to = Place::Base, .n = 2});
    EXPECT_EQ(runes.at(0).code, "R2");
    f.run({.kind = CommandKind::Exhaust, .ids = {runes[0].id, runes[1].id}, .exhausted = true});
    f.run({.kind = CommandKind::Move, .to = Place::RunesBottom, .id = runes[0].id});
    EXPECT_EQ(f.table.tracker().belief().runes, 2);
    EXPECT_EQ(f.sim.pile(Pile::Runes).front().code, "R2");

    const auto unit = f.draw(1).at(0);
    f.play(unit.id);
    f.run({.kind = CommandKind::Exhaust, .ids = {unit.id}, .exhausted = true});
    f.run({.kind = CommandKind::Move, .to = Place::Bf1, .id = unit.id});  // board to board: stays exhausted
    EXPECT_TRUE(f.table.tracker().belief().spots[static_cast<int>(Place::Bf1)].at(0).exhausted);
    f.run({.kind = CommandKind::Move, .to = Place::Trash, .id = unit.id});  // dies: ready in the trash
    EXPECT_FALSE(f.table.tracker().belief().spots[static_cast<int>(Place::Trash)].at(0).exhausted);
}

TEST(TableTracker, TokensComeAndGo) {
    Fixture f(setup_of({"A"}));
    const auto t = f.run({.kind = CommandKind::Token, .to = Place::Base, .code = "token:Bird"}).at(0);
    f.run({.kind = CommandKind::Move, .to = Place::Bf0, .id = t.id});
    f.run({.kind = CommandKind::Remove, .id = t.id});
    EXPECT_TRUE(f.table.tracker().belief().spots[static_cast<int>(Place::Bf0)].empty());
}

TEST(TableTracker, PeekToTopOrBottom) {
    Fixture f(setup_of({"C", "B", "A"}));
    EXPECT_EQ(f.run({.kind = CommandKind::Peek}).at(0).code, "A");
    f.run({.kind = CommandKind::PeekDone, .to = Place::DeckBottom});
    EXPECT_EQ(f.sim.pile(Pile::Deck).front().code, "A");
    EXPECT_EQ(f.run({.kind = CommandKind::Peek}).at(0).code, "B");
    f.run({.kind = CommandKind::PeekDone, .to = Place::DeckTop});
    EXPECT_EQ(f.draw(1).at(0).code, "B");
}

TEST(TableTracker, BurnOutShufflesTheTrashIntoTheDeck) {
    Fixture f(setup_of({"B", "A"}));
    const auto burned = f.run({.kind = CommandKind::Deal, .to = Place::Trash, .n = 2});
    EXPECT_EQ(f.table.tracker().belief().deck, 0);
    f.run({.kind = CommandKind::ShuffleTrashIntoDeck, .order = {burned[0].id, burned[1].id}});
    EXPECT_EQ(f.table.tracker().belief().deck, 2);
    EXPECT_EQ(f.draw(1).at(0).code, "B");  // order: A at the bottom, B on top
}

TEST(TableTracker, MulliganToTheBottomAndRescan) {
    Fixture f(setup_of({"E", "D", "C", "B", "A"}));
    const auto cards = f.draw(4);
    f.run({.kind = CommandKind::Play, .to = Place::DeckBottom, .id = cards[1].id});
    EXPECT_EQ(f.sim.pile(Pile::Deck).front().code, "B");
    f.run({.kind = CommandKind::RescanHand});
    EXPECT_EQ(f.corrections, 0);  // nothing to correct
    EXPECT_EQ(f.counts().recoveries, 1);
}

TEST(TableTracker, BadCommandsFailWithoutTouchingAnything) {
    Fixture f(setup_of({"B", "A"}));
    const auto cards = f.draw(1);
    EXPECT_EQ(f.fail({.kind = CommandKind::Play, .to = Place::Base, .id = 99}).reason, FailReason::Invalid);
    EXPECT_EQ(f.fail({.kind = CommandKind::Move, .to = Place::Base, .id = cards[0].id}).reason, FailReason::Invalid);
    EXPECT_EQ(f.fail({.kind = CommandKind::Exhaust, .ids = {1, 99}}).reason, FailReason::Invalid);
    EXPECT_EQ(f.fail({.kind = CommandKind::PeekDone, .to = Place::DeckTop}).reason, FailReason::Invalid);
    EXPECT_EQ(f.fail({.kind = CommandKind::Deal, .to = Place::Hand, .n = 2}).reason, FailReason::Invalid);
    EXPECT_FALSE(f.table.tracker().belief().spots[static_cast<int>(Place::Legend)].at(0).exhausted);
}

TEST(TableTracker, StateShowsTheBelief) {
    Fixture f(setup_of({"B", "A"}));
    f.draw(1);
    const auto s = f.table.state();
    EXPECT_EQ(s["hand"].size(), 1u);
    EXPECT_EQ(s["deck"], 1);
    EXPECT_EQ(s["spots"]["legend"][0]["code"], "LEG");
    EXPECT_TRUE(s["held"].is_null());
}

