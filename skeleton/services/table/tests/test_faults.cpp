// Property tests (#41): random command sequences against the sim, with each fault kind alone and all together.
// What must hold against the sim's truth: every card that leaves the hand is the card the table reported; after every
// recovery the hand is the truth; after a final rescan the whole belief is; every command ends in exactly one
// done/failed for its seq; a fault-free run never recovers unasked. Seeds are fixed: the runs are deterministic.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <random>

#include "sim.hpp"
#include "table.hpp"

using namespace buddy::table;

namespace {

SimSetup deck_setup(std::uint64_t seed) {
    // 40 cards of 14 codes (a deck has up to 3 copies), 12 runes of 2
    SimSetup s;
    for (int i = 0; i < 40; ++i) s.deck.push_back("C" + std::to_string(i % 14));
    for (int i = 0; i < 12; ++i) s.runes.push_back(i % 2 ? "RUNE-F" : "RUNE-O");
    std::mt19937_64 rng(seed);
    std::shuffle(s.deck.begin(), s.deck.end(), rng);
    std::shuffle(s.runes.begin(), s.runes.end(), rng);
    s.legend = "LEG";
    s.champion = "CHAMP";
    return s;
}

struct Outcome {
    int commands = 0, plays = 0, failed = 0, hardware = 0, corrections = 0;
    long recoveries = 0, unasked_recoveries = 0;
    std::vector<std::string> problems;
};

// A random game-like sequence: draws, plays to every kind of place, returns, runes, exhaust, peeks, tokens, burns,
// Burn Out, rescans. Commands only name cards the belief has.
Outcome run_sequence(SimFaults faults, std::uint64_t seed, TrackerConfig cfg, int length = 300) {
    const SimSetup setup = deck_setup(seed);
    Sim sim(setup, faults, seed * 7 + 1);
    Table table(sim, cfg);
    std::mt19937_64 rng(seed);
    const auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    Outcome out;
    std::int64_t seq = 0;
    const auto& belief = table.tracker().belief();
    const auto spot_cards = [&](std::initializer_list<Place> places) {
        std::vector<int> ids;
        for (const Place p : places) {
            for (const Known& k : belief.spots[static_cast<std::size_t>(p)]) ids.push_back(k.id);
        }
        return ids;
    };

    const auto run = [&](Command c) {
        c.seq = ++seq;
        ++out.commands;
        const long before = table.tracker().counts().recoveries;
        const auto events = table.execute(c);
        const std::string what = std::string(to_string(c.kind)) + " #" + std::to_string(c.seq);
        int ends = 0;
        for (const Event& e : events) {
            if (e.seq != c.seq) out.problems.push_back(what + ": event for another seq");
            if (e.kind != EventKind::HandCorrected) ++ends;
            else {
                ++out.corrections;
                if (std::getenv("TABLE_DEBUG")) std::cerr << what << " corrected: " << to_json(e).dump() << "\n";
            }
        }
        if (ends != 1 || events.back().kind == EventKind::HandCorrected) out.problems.push_back(what + ": not one end");
        const Event& end = events.back();
        if (end.kind == EventKind::Failed) {
            ++out.failed;
            if (end.reason == FailReason::Hardware) ++out.hardware;
            if (std::getenv("TABLE_DEBUG")) std::cerr << what << " failed: " << end.message << "\n";
            if (end.reason == FailReason::Invalid) out.problems.push_back(what + ": invalid: " + end.message);
        }
        if (c.kind == CommandKind::Play && end.kind == EventKind::Done) {
            ++out.plays;
            if (sim.placed().empty() || sim.placed().front() != end.cards.at(0).code) {
                out.problems.push_back(what + ": played " + (sim.placed().empty() ? "nothing" : sim.placed().front()) +
                                       ", reported " + end.cards.at(0).code);
            }
        }
        const long recovered = table.tracker().counts().recoveries - before;
        out.recoveries += recovered;
        if (c.kind != CommandKind::RescanHand) out.unasked_recoveries += recovered;
        if (recovered > 0 && end.kind == EventKind::Done) {
            if (auto diff = sim.compare(belief, table.tracker().hand_pile(), true); !diff.empty()) {
                out.problems.push_back(what + ": after recovery " + diff);
            }
        }
        return end;
    };

    run({.kind = CommandKind::Setup, .legend = setup.legend, .champion = setup.champion,
         .deck = static_cast<int>(setup.deck.size()), .runes = static_cast<int>(setup.runes.size())});
    run({.kind = CommandKind::Deal, .to = Place::Hand, .n = 4});
    for (int i = 0; i < length; ++i) {
        const auto& hand = belief.hand;
        switch (pick(12)) {
            case 0: case 1:
                if (belief.deck > 0) run({.kind = CommandKind::Deal, .to = Place::Hand, .n = std::min(belief.deck, 1 + static_cast<int>(pick(2)))});
                break;
            case 2: case 3: case 4:
                if (!hand.empty()) {
                    static constexpr Place kTo[] = {Place::Base, Place::Bf0, Place::Chain, Place::Trash,
                                                    Place::DeckBottom, Place::Banish};
                    run({.kind = CommandKind::Play, .to = kTo[pick(6)], .id = hand[pick(hand.size())].id});
                }
                break;
            case 5: {
                const auto ids = spot_cards({Place::Base, Place::Bf0, Place::Chain, Place::Trash});
                if (ids.empty()) break;
                static constexpr Place kTo[] = {Place::Hand, Place::Trash, Place::Bf1, Place::DeckBottom};
                run({.kind = CommandKind::Move, .to = kTo[pick(4)], .id = ids[pick(ids.size())]});
                break;
            }
            case 6:
                if (belief.runes > 0) run({.kind = CommandKind::Deal, .from = Source::Runes, .to = Place::Base, .n = 1});
                break;
            case 7: {
                const auto ids = spot_cards({Place::Base, Place::Bf0, Place::Bf1, Place::Legend});
                if (ids.empty()) break;
                run({.kind = CommandKind::Exhaust, .ids = {ids[pick(ids.size())]}, .exhausted = pick(2) == 0});
                break;
            }
            case 8:
                if (belief.deck > 0) {
                    run({.kind = CommandKind::Peek});
                    if (belief.held) run({.kind = CommandKind::PeekDone, .to = pick(2) ? Place::DeckTop : Place::DeckBottom});
                }
                break;
            case 9: {
                const auto ids = spot_cards({Place::Base});
                if (pick(2) || ids.empty()) run({.kind = CommandKind::Token, .to = Place::Base, .code = "token:Bird"});
                else run({.kind = CommandKind::Move, .to = Place::RunesBottom, .id = ids[0]});  // a rune or a unit: the sim doesn't care
                break;
            }
            case 10:
                if (belief.deck > 0) run({.kind = CommandKind::Deal, .to = Place::Trash, .n = 1});
                else if (!belief.spots[static_cast<std::size_t>(Place::Trash)].empty())
                    run({.kind = CommandKind::ShuffleTrashIntoDeck});
                break;
            case 11:
                if (pick(4) == 0) run({.kind = CommandKind::RescanHand});
                break;
        }
    }
    run({.kind = CommandKind::RescanHand});
    if (auto diff = sim.compare(belief, table.tracker().hand_pile()); !diff.empty()) {
        out.problems.push_back("at the end: " + diff);
    }
    return out;
}

void expect_holds(const std::string& name, SimFaults faults, int seeds, TrackerConfig cfg = {.confirm_reads = 3}) {
    Outcome total;
    for (int s = 0; s < seeds; ++s) {
        const Outcome o = run_sequence(faults, static_cast<std::uint64_t>(s), cfg);
        for (const auto& p : o.problems) ADD_FAILURE() << name << " seed " << s << ": " << p;
        total.plays += o.plays;
        total.failed += o.failed;
        total.hardware += o.hardware;
        total.recoveries += o.recoveries;
        total.corrections += o.corrections;
    }
    EXPECT_EQ(total.hardware, 0) << name;
    EXPECT_GT(total.plays, seeds * 50) << name;
    std::cout << name << ": " << total.plays << " plays, " << total.recoveries << " recoveries, " << total.corrections
              << " corrections, " << total.failed << " failed\n";
}

} // namespace

TEST(TableFaults, FaultFreeNeverRecoversUnasked) {
    for (int s = 0; s < 20; ++s) {
        const Outcome o = run_sequence({}, static_cast<std::uint64_t>(s), {});
        for (const auto& p : o.problems) ADD_FAILURE() << "seed " << s << ": " << p;
        EXPECT_EQ(o.unasked_recoveries, 0);
        EXPECT_EQ(o.failed, 0);
        EXPECT_EQ(o.corrections, 0);
    }
}

TEST(TableFaults, Misread) { expect_holds("misread", {.misread = 0.05}, 30); }
TEST(TableFaults, Unreadable) { expect_holds("unreadable", {.unreadable = 0.1}, 30); }
TEST(TableFaults, DoubleFeed) { expect_holds("double feed", {.double_feed = 0.05}, 30); }
TEST(TableFaults, MissedFeed) { expect_holds("missed feed", {.missed_feed = 0.05}, 30); }
TEST(TableFaults, DoubleTake) { expect_holds("double take", {.double_take = 0.05}, 30); }
TEST(TableFaults, DoubleDeal) { expect_holds("double deal", {.double_deal = 0.05}, 30); }
TEST(TableFaults, Disturbed) { expect_holds("disturbed", {.disturbed = 0.05}, 30); }
TEST(TableFaults, AllTogether) { expect_holds("all", parse_faults("stress"), 50); }

TEST(TableFaults, ParseRates) {
    EXPECT_EQ(parse_faults(""), SimFaults{});
    EXPECT_EQ(parse_faults("none"), SimFaults{});
    EXPECT_EQ(parse_faults("misread=0.1,double_take=0.5"), (SimFaults{.misread = 0.1, .double_take = 0.5}));
    const SimFaults stress = parse_faults("stress");
    EXPECT_EQ(stress.unreadable, 0.05);
    EXPECT_EQ(parse_faults("stress,misread=0"), (SimFaults{.unreadable = 0.05, .double_feed = 0.02, .missed_feed = 0.02,
                                                            .double_take = 0.01, .double_deal = 0.01, .disturbed = 0.01}));
    EXPECT_EQ(parse_faults(to_string(stress)), stress);
    EXPECT_THROW(parse_faults("misread"), std::invalid_argument);
    EXPECT_THROW(parse_faults("misread=x"), std::invalid_argument);
    EXPECT_THROW(parse_faults("misread=2"), std::invalid_argument);
    EXPECT_THROW(parse_faults("wobble=0.1"), std::invalid_argument);
}

TEST(TableFaults, SameSeedSameRun) {
    const Outcome a = run_sequence(parse_faults("stress"), 3, {});
    const Outcome b = run_sequence(parse_faults("stress"), 3, {});
    EXPECT_EQ(a.recoveries, b.recoveries);
    EXPECT_EQ(a.plays, b.plays);
}
