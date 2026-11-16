// Session games (5.6.2, #48; Dev's real cards #49): Buddy's side through an in-process table_core Table + Sim, Dev a
// SimDev (greedy; the #48 tests with the placeholders as its truth, the SimDev games with a true shuffled deck). After
// every step Buddy's cards as the engine sees them = the table's belief = the sim's truth, Dev's cards Buddy knows =
// SimDev's truth, and every card of Dev's that Buddy learned was prompted or named; at the end the session's replay
// reproduces its state. Fault-free games, each fault alone, every failure path forced through a Backend wrapper, and
// 50 SimDev games.
#include <gtest/gtest.h>

#include "session_game.hpp"

TEST(RiftboundSession, FaultFreeGamesPlayToTheEnd) {
    for (const auto& [d0, d1] : kDecks) {
        for (PlayerId buddy = 0; buddy < 2; ++buddy) {
            Game g(d0, d1, buddy, 100 + buddy);
            g.run();
            SCOPED_TRACE(std::string(d0) + " vs " + d1 + ", Buddy seat " + std::to_string(buddy));
            EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
            EXPECT_TRUE(g.problems.empty()) << g.problems.front();
            EXPECT_TRUE(g.pauses.empty());
            EXPECT_GT(g.checked, 50);
            const auto& st = g.session->stats();
            EXPECT_GT(st.binds, 10);
            EXPECT_GT(st.reapplies, 0);  // the sim's deck order isn't the engine's
            EXPECT_EQ(st.re_decides + st.retries + st.corrections + st.extras, 0);
            EXPECT_EQ(g.replay_problem(), "");
            EXPECT_EQ(g.final_check(), "");
            EXPECT_TRUE(has_voice(g, "Let's play"));
        }
    }
}

// Each sim fault alone at five times the stress rate (at least 10 %): no desync, every game ends, the checks hold after every step.
TEST(RiftboundSession, EachFaultAlone) {
    const table::SimFaults stress = table::parse_faults("stress");
    const std::vector<std::pair<std::string, double table::SimFaults::*>> faults{
        {"misread", &table::SimFaults::misread},         {"unreadable", &table::SimFaults::unreadable},
        {"double_feed", &table::SimFaults::double_feed}, {"missed_feed", &table::SimFaults::missed_feed},
        {"double_take", &table::SimFaults::double_take}, {"double_deal", &table::SimFaults::double_deal},
        {"disturbed", &table::SimFaults::disturbed}};
    for (const auto& [name, field] : faults) {
        game::SessionStats sum;
        long injected = 0;
        for (int i = 0; i < 2; ++i) {
            table::SimFaults f;
            f.*field = std::max(0.1, 5 * (stress.*field));
            const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i)];
            Game g(d0, d1, static_cast<PlayerId>(i), 200 + static_cast<std::uint64_t>(i), f, 7 + static_cast<std::uint64_t>(i));
            g.run();
            SCOPED_TRACE(name + " game " + std::to_string(i));
            EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
            EXPECT_TRUE(g.problems.empty()) << g.problems.front();
            EXPECT_EQ(std::count(g.pauses.begin(), g.pauses.end(), Pause::Desync), 0);
            EXPECT_EQ(g.replay_problem(), "");
            EXPECT_EQ(g.final_check(), "");
            const auto& st = g.session->stats();
            sum.retries += st.retries;
            sum.corrections += st.corrections;
            sum.reorders += st.reorders;
            sum.pauses += st.pauses;
            injected += g.sim.faults().total();
        }
        EXPECT_GT(injected, 0) << name;
        // unreadable alone rarely fails a deal (reads vote over up to 10 scans): UnreadableDealIsRetriedOnce forces it
        if (name == "disturbed") {
            EXPECT_GT(sum.reorders, 0);
        }
        std::cout << name << ": injected " << injected << ", retries " << sum.retries << ", corrections "
                  << sum.corrections << ", reorders " << sum.reorders << ", pauses " << sum.pauses << "\n";
    }
}

// A misread at draw that the votes accepted: Buddy plays the card, the table finds another one there (not_in_hand
// after a recovery that replaced it), the session rebinds it, says so, and the AI decides again.
TEST(RiftboundSession, MisreadAtDrawIsReboundAndBuddyDecidesAgain) {
    Game g("vi-unleashed", "zed-vendetta", 0, 300);
    const PlayerId b = 0;
    int deals = 0;
    int misread_id = 0;
    std::string truth;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind != CommandKind::Deal || c.to != TPlace::Hand || ++deals != 5) return;  // the last opening card
        truth = g.sim.pile(Pile::Deck).back().code;
        g.flaky.misread_as = cheap_unit(g.session->state(), b, truth);
        g.flaky.misread = 1;
    };
    g.after_cmd = [&](const table::Command& c, const std::vector<table::Event>& events) {
        if (c.kind == CommandKind::Deal && deals == 5 && misread_id == 0) misread_id = events.back().cards.at(0).id;
    };
    g.buddy_choice = [&](const game::AiRequest& r) -> std::optional<Action> {
        const ObjId obj = g.session->obj_of(misread_id);
        for (const Action& a : r.actions) {
            if (a.kind == ActionKind::Play && a.obj == obj) return a;
        }
        return std::nullopt;
    };
    g.check_counts = false;  // the hand is wrong on purpose until Buddy plays the card
    g.stop_when = [&] { return g.session->stats().re_decides > 0; };
    g.run();
    ASSERT_EQ(g.session->stats().re_decides, 1) << g.session->pause_message();
    ASSERT_FALSE(g.flaky.misread_as.empty());
    EXPECT_EQ(g.session->stats().misreads, 1);
    const ObjId obj = g.session->obj_of(misread_id);
    ASSERT_NE(obj, no_obj);
    EXPECT_EQ(game::code_of(g.session->state(), obj), truth);
    g.stop_when = nullptr;
    g.buddy_choice = nullptr;
    g.check_counts = true;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_TRUE(has_voice(g, "misread"));
    EXPECT_EQ(g.replay_problem(), "");
}

TEST(RiftboundSession, UnreadableDealIsRetriedOnce) {
    Game g("zed-vendetta", "vi-unleashed", 1, 310);
    int deals = 0;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Deal && ++deals == 8) g.flaky.unreadable = 1;
    };
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.session->stats().retries, 1);
    EXPECT_TRUE(g.pauses.empty());
    EXPECT_EQ(g.replay_problem(), "");
}

TEST(RiftboundSession, UnreadableTwicePausesUntilResume) {
    Game g("vi-unleashed", "zed-vendetta", 1, 320);
    int deals = 0;
    long steps_at_pause = -1;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Deal && ++deals == 8) g.flaky.unreadable = 2;
    };
    g.resumes_left = 0;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Unreadable);
    EXPECT_TRUE(has_voice(g, "can't read"));
    steps_at_pause = g.session->stats().steps;
    const GameState before = g.session->state();
    EXPECT_THROW(g.session->on_dev(game::DevOption{}), std::logic_error);
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_GT(g.session->stats().steps, steps_at_pause);
    EXPECT_EQ(g.session->stats().resumes, 1);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// The table fails mid-step: the state doesn't commit; after resume (rescan) the step goes on where it stopped.
TEST(RiftboundSession, HardwareFailurePausesMidStep) {
    Game g("zed-vendetta", "vi-unleashed", 0, 330);
    int plays = 0;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Play && ++plays == 3) g.flaky.hardware = 1;
    };
    g.resumes_left = 0;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Hardware);
    const long steps = g.session->stats().steps;
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_GT(g.session->stats().steps, steps);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// A hand card disappears: the next play recovers and reports it missing, the session pauses; once Dev puts it back
// the rescan finds it as an extra card, which is the lost one: same object, the game goes on.
TEST(RiftboundSession, MissingCardPausesUntilItIsBack) {
    Game g("vi-unleashed", "zed-vendetta", 0, 340);
    std::string lost;
    g.stop_when = [&] {
        return g.session->status() == Status::BuddyThinking && g.table.tracker().belief().hand.size() >= 3 &&
               g.session->stats().steps > 10;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::BuddyThinking);
    const Pile h = g.table.tracker().hand_pile();
    lost = g.sim.pile(h).front().code;
    g.sim.take(h);  // the bottom card off the hand, gone
    g.sim.place(TPlace::Banish, 9999);
    g.sim.remove(9999);
    g.stop_when = nullptr;
    g.resumes_left = 0;
    g.check_counts = false;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused) << (g.problems.empty() ? "" : g.problems.front());
    EXPECT_EQ(g.session->pause(), Pause::Missing);
    // resumed without the card: still missing
    g.resumes_left = 1;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Missing);
    g.sim.token(TPlace::Base, 9998, lost);  // Dev found it: back on the hand pile
    g.sim.put(9998, g.table.tracker().hand_pile(), table::End::Top);
    g.check_counts = true;
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// A card nobody knew of in the hand (an undetected double deal): back on the deck top.
TEST(RiftboundSession, ExtraCardGoesBackOnTheDeckTop) {
    Game g("zed-vendetta", "vi-unleashed", 1, 350);
    g.stop_when = [&] { return g.session->status() == Status::BuddyThinking && g.session->stats().steps > 10; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::BuddyThinking);
    g.sim.deal(Pile::Deck);
    g.sim.place(g.table.tracker().hand_pile(), table::End::Bottom);
    g.stop_when = [&] { return g.session->stats().extras > 0 && !g.session->state().winner; };
    g.check_counts = false;  // the belief counts the dealt card twice (deck and extra): the tracker's business
    g.run();
    EXPECT_EQ(g.session->stats().extras, 1);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
}

// Hand piles disturbed until a recovery notices: the engine adopts the table's order (an order line in the replay).
TEST(RiftboundSession, ChangedHandOrderIsAdopted) {
    Game g("vi-unleashed", "zed-vendetta", 1, 360);
    g.sim.rates().disturbed = 0.3;
    g.stop_when = [&] { return g.session->stats().reorders > 0; };
    g.run();
    ASSERT_GT(g.session->stats().reorders, 0) << g.session->pause_message();
    g.sim.rates().disturbed = 0;
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_NE(g.session->replay_text().find("\norder 1 "), std::string::npos);
    EXPECT_EQ(g.replay_problem(), "");
}

// A re-applied step whose commands no longer start with the ones already run: desync, paused for good, replay noted.
TEST(RiftboundSession, DesyncIsDetected) {
    Game g("zed-vendetta", "vi-unleashed", 0, 370);
    long seen = 0;  // re-applies count up only for re-applications, not a step's first computation
    bool done = false;
    g.session->on_moves = [&](std::vector<game::TableMove>& list) {
        const long now = g.session->stats().reapplies;
        const bool reapplied = now > seen;
        seen = now;
        if (done || !reapplied || g.session->stats().steps < 5 || list.empty()) return;
        list[0].cmd.to = list[0].cmd.to == TPlace::Trash ? TPlace::Base : TPlace::Trash;
        done = true;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Desync);
    EXPECT_NE(g.session->replay_text().find("# desync: command 1 was "), std::string::npos);
    EXPECT_THROW(g.session->resume(), std::logic_error);
    g.session->concede(1);
    EXPECT_EQ(g.session->status(), Status::Over);
}

TEST(RiftboundSession, DevActionsAreChecked) {
    Game g("vi-unleashed", "zed-vendetta", 1, 380);
    g.stop_when = [&] { return g.session->status() == Status::DevToAct; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToAct);
    const GameState before = g.session->state();
    EXPECT_THROW(g.session->on_dev({.action = {.kind = ActionKind::Play, .obj = 9999}}), game::Refused);
    EXPECT_TRUE(g.session->state() == before);
    EXPECT_EQ(g.session->take().table.size(), 0u);
    g.session->concede(0);
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_EQ(g.session->state().winner, std::optional<PlayerId>(1));
}

// #49: Dev names their battlefield first (before both mulligans); impossible answers are refused and change nothing.
TEST(RiftboundSession, DevNamesTheBattlefieldFirst) {
    Game g("vi-unleashed", "zed-vendetta", 0, 390);
    g.stop_when = [&] { return g.session->status() == Status::DevToReveal; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToReveal);
    const game::DevPrompt p = *g.session->prompt();
    ASSERT_EQ(p.kind, game::DevPrompt::Kind::Battlefield);
    ASSERT_EQ(p.objs.size(), 1u);
    EXPECT_EQ(g.session->state().decision.kind, DecisionKind::Mulligan);
    EXPECT_EQ(g.session->stats().steps, 0);
    const GameState& s = g.session->state();
    const ObjId field = p.objs[0];
    g.check_truth = false;  // the test names another battlefield than SimDev's
    const Card* other = nullptr;  // one of Dev's battlefields the engine didn't pick
    for (const Object& o : s.objects) {
        if (o.owner == g.dev_seat() && o.zone == Zone::Removed && o.card->type == CardType::Battlefield) other = o.card;
    }
    ASSERT_NE(other, nullptr);
    const GameState before = s;
    const std::string replay = g.session->replay_text();
    const Card* buddys = s.obj(s.battlefields[0].card).card;
    const Card* rune = s.obj(s.player(g.dev_seat()).rune_deck.back()).card;
    for (const auto& wrong : std::vector<std::vector<const Card*>>{{}, {other, other}, {buddys}, {rune}}) {
        EXPECT_THROW(g.session->on_reveal(wrong), game::Refused);
        EXPECT_TRUE(g.session->state() == before);
        EXPECT_EQ(g.session->replay_text(), replay);
        EXPECT_EQ(g.session->status(), Status::DevToReveal);
    }
    EXPECT_EQ(g.session->stats().refusals, 4);
    g.session->on_reveal({other});
    EXPECT_EQ(g.session->state().obj(field).card, other);
    EXPECT_NE(g.session->replay_text().find("bind " + std::to_string(field) + " " + other->home().code.str()),
              std::string::npos);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// #49: channelled runes and burned cards are prompts; a card Dev can't have left or the wrong kind is refused.
TEST(RiftboundSession, DevPromptsRefuseImpossibleCards) {
    Game g("zed-vendetta", "vi-unleashed", 1, 391);
    std::map<game::DevPrompt::Kind, int> seen;
    bool tried_runes = false;
    g.stop_when = [&] {
        if (g.session->status() != Status::DevToReveal) return false;
        const auto& p = *g.session->prompt();
        ++seen[p.kind];
        if (p.kind != game::DevPrompt::Kind::Runes || tried_runes) return false;
        tried_runes = true;
        return true;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToReveal);
    const GameState before = g.session->state();
    const game::DevPrompt p = *g.session->prompt();
    for (const ObjId id : p.objs) {
        EXPECT_EQ(before.obj(id).zone, Zone::RuneDeck);
        EXPECT_FALSE(before.obj(id).known & 1u << g.session->buddy());
    }
    const Card* legend = before.obj(before.player(g.dev_seat()).legend.front()).card;
    std::vector<const Card*> wrong(p.objs.size(), legend);
    EXPECT_THROW(g.session->on_reveal(wrong), game::Refused);
    EXPECT_TRUE(g.session->state() == before);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
    EXPECT_GT(g.session->stats().prompts, 3);
}

// #50: Dev's options (and so the panel's `i`) are the same for every arrangement of Dev's placeholders and of
// Buddy's hidden cards: they show Dev nothing the referee made up, and an index stays valid when they change.
TEST(RiftboundSession, DevOptionsDontDependOnHiddenCards) {
    for (int i = 0; i < 2; ++i) {
        const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i)];
        Game g(d0, d1, static_cast<PlayerId>(i), 600 + static_cast<std::uint64_t>(i), {}, 3, true);
        Rng rng(77);
        int checked = 0;
        const auto key = [](const std::vector<game::DevOption>& list) {
            std::vector<std::tuple<Action, const Card*>> out;
            for (const auto& o : list) out.emplace_back(o.action, o.named ? o.card : nullptr);
            return out;
        };
        g.stop_when = [&] {
            if (g.session->status() != Status::DevToAct) return false;
            const GameState& s = g.session->state();
            const auto kept = g.session->kept();
            const auto want = key(g.session->dev_options());
            const PlayerId b = g.session->buddy();
            for (int k = 0; k < 3; ++k) {
                GameState r = rearranged(s, g.dev_seat(), b, dev_fixed(*g.session), rng);
                r = rearranged(r, b, g.dev_seat(), {}, rng);
                const auto got = key(game::dev_options(r, {b, kept, &g.session->named(), &g.cfg.decks[g.dev_seat()]}));
                if (got != want) {
                    g.problems.push_back("turn " + std::to_string(s.turn) + ": options differ");
                    return true;
                }
            }
            ++checked;
            return false;
        };
        g.run();
        EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_GT(checked, 30);
    }
}

// #49's gate: SimDev (a true shuffled deck, greedy) against Buddy (greedy), both decks from both seats, fault-free
// table. No refusal, no desync, no pause; Dev's options always hold SimDev's true move, the offered cards are exactly
// the possible ones, every card Buddy learns of Dev's comes from a prompt or Dev's choice and is SimDev's true card,
// and the game with Dev's true cards from the start is the same legal game. 50 games in 5 tests (ctest runs them in
// parallel).
void sim_dev_games(int first) {
    game::SessionStats sum;
    int collapsed = 0;
    for (int i = first; i < first + 10; ++i) {
        const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i % 2)];
        const auto buddy = static_cast<PlayerId>(i / 2 % 2);
        Game g(d0, d1, buddy, 1000 + static_cast<std::uint64_t>(i), {}, 11 + static_cast<std::uint64_t>(i), true);
        g.check_candidates = true;
        g.run();
        SCOPED_TRACE("game " + std::to_string(i) + ": " + d0 + " vs " + d1 + ", Buddy seat " + std::to_string(buddy));
        EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_TRUE(g.pauses.empty());
        const auto& st = g.session->stats();
        EXPECT_EQ(st.refusals, 0);
        EXPECT_EQ(g.replay_problem(), "");
        EXPECT_EQ(g.true_replay_problem(), "");
        EXPECT_EQ(g.final_check(), "");
        sum.dev_decisions += st.dev_decisions;
        sum.dev_forced += st.dev_forced;
        sum.dev_named += st.dev_named;
        sum.dev_assumed += st.dev_assumed;
        sum.prompts += st.prompts;
        collapsed += g.collapsed;
    }
    EXPECT_GT(sum.dev_named, 20);
    EXPECT_GT(sum.prompts, 20);
    std::cout << "SimDev games " << first << "–" << first + 9 << ": Dev decisions " << sum.dev_decisions << ", forced "
              << sum.dev_forced << ", named " << sum.dev_named << ", assumed " << sum.dev_assumed << ", prompts " << sum.prompts << ", choices collapsed into a prompt " << collapsed << "\n";
}

TEST(RiftboundSession, SimDevGames0) { sim_dev_games(0); }
TEST(RiftboundSession, SimDevGames1) { sim_dev_games(10); }
TEST(RiftboundSession, SimDevGames2) { sim_dev_games(20); }
TEST(RiftboundSession, SimDevGames3) { sim_dev_games(30); }
TEST(RiftboundSession, SimDevGames4) { sim_dev_games(40); }
