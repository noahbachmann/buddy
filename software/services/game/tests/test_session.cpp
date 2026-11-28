// Session games (2.6.2, #48; Dev's real cards #49): Buddy's side through an in-process table_core Table + Sim, Dev a
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

// The table's answer doesn't come (#52): the service's clock pauses the game with the command still in flight.
// A late answer goes on as if it had come in time; resume instead resends the command with its seq, and the table
// answers it once whether it ran or not.
TEST(RiftboundSession, TableTimeoutWaitsForALateAnswer) {
    Game g("vi-unleashed", "zed-vendetta", 0, 380);
    int plays = 0;
    g.lose_answer = [&](const table::Command& c) { return c.kind == CommandKind::Play && ++plays == 4; };
    g.run();
    ASSERT_FALSE(g.lost.empty());
    ASSERT_NE(g.session->inflight(), nullptr);
    g.session->table_timeout();
    EXPECT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Timeout);
    EXPECT_TRUE(g.session->resumable());
    EXPECT_NE(g.session->inflight(), nullptr);
    g.session->table_timeout();  // once paused, nothing more
    EXPECT_EQ(g.session->stats().pauses, 1);
    for (const auto& e : g.lost) g.session->on_table(e);
    g.lost.clear();
    g.lose_answer = nullptr;
    EXPECT_NE(g.session->status(), Status::Paused);
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.session->stats().resumes, 0);
    EXPECT_EQ(g.replay_problem(), "");
}

TEST(RiftboundSession, TableTimeoutResumeResendsTheSameCommand) {
    for (const bool ran : {true, false}) {  // the table ran it and the answer got lost, or it never got the command
        Game g("zed-vendetta", "vi-unleashed", 1, ran ? 390 : 391);
        int deals = 0;
        g.lose_answer = [&](const table::Command& c) { return c.kind == CommandKind::Deal && ++deals == 9; };
        g.lose_command = !ran;
        g.run();
        ASSERT_FALSE(g.lost.empty());
        const table::Command cmd = *g.session->inflight();
        const auto& b = g.table.tracker().belief();
        const int cards = b.deck + b.runes;
        g.session->table_timeout();
        ASSERT_EQ(g.session->pause(), Pause::Timeout);
        g.lose_answer = nullptr;
        std::vector<table::Command> sent;
        g.after_cmd = [&](const table::Command& c, const std::vector<table::Event>&) {
            if (sent.empty()) {
                EXPECT_EQ(b.deck + b.runes, ran ? cards : cards - cmd.n);
            }
            sent.push_back(c);
        };
        g.session->resume();
        g.run();
        ASSERT_FALSE(sent.empty());
        EXPECT_EQ(sent[0], cmd);  // the same seq: the table dealt once either way
        EXPECT_EQ(g.session->status(), Status::Over);
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_EQ(g.session->stats().resumes, 1);
        EXPECT_TRUE(has_voice(g, "isn't answering"));
        EXPECT_EQ(g.replay_problem(), "");
        EXPECT_EQ(g.final_check(), "");
    }
}

// The table restarted mid-game: the sim's piles are gone, the game can't go on.
TEST(RiftboundSession, TableRestartPausesForGood) {
    Game g("vi-unleashed", "zed-vendetta", 1, 395);
    g.stop_when = [&] { return g.session->stats().steps >= 20; };
    g.run();
    g.session->table_restarted();
    EXPECT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::TableRestart);
    EXPECT_FALSE(g.session->resumable());
    EXPECT_EQ(g.session->inflight(), nullptr);
    EXPECT_THROW(g.session->resume(), std::logic_error);
    g.session->table_timeout();  // stays a restart
    EXPECT_EQ(g.session->pause(), Pause::TableRestart);
    const auto out = g.session->take();
    EXPECT_NE(std::find(out.voice.begin(), out.voice.end(), game::pause_text(Pause::TableRestart)), out.voice.end());
    g.session->concede(g.session->buddy());
    EXPECT_EQ(g.session->status(), Status::Over);
    g.session->table_restarted();  // after the game: nothing
    EXPECT_EQ(g.session->status(), Status::Over);
}

// The table restarted while the game service was down (a reboot): its first answer is no_game.
TEST(RiftboundSession, NoGameOnTheTablePausesForGood) {
    Game g("zed-vendetta", "vi-unleashed", 0, 396);
    int plays = 0;
    g.lose_answer = [&](const table::Command& c) { return c.kind == CommandKind::Play && ++plays == 2; };
    g.lose_command = true;
    g.run();
    ASSERT_NE(g.session->inflight(), nullptr);
    g.session->on_table({.kind = table::EventKind::Failed, .seq = g.session->inflight()->seq,
                         .reason = table::FailReason::NoGame, .message = "no game on the table"});
    EXPECT_EQ(g.session->pause(), Pause::TableRestart);
    EXPECT_FALSE(g.session->resumable());
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
    g.stop_when = [&] {  // SimDev names the champion first (#98)
        return g.session->status() == Status::DevToReveal && g.session->prompt()->kind == game::DevPrompt::Kind::Battlefield;
    };
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

// #98: Dev names their Chosen Champion before the battlefield; only one of the deck's champion options is taken, and
// the decklist's champion, put back, is one Dev can still hold and play.
TEST(RiftboundSession, DevNamesTheChampionFirst) {
    Game g("vi-unleashed", "zed-vendetta", 0, 392);
    g.stop_when = [&] { return g.session->status() == Status::DevToReveal; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToReveal);
    const game::DevPrompt p = *g.session->prompt();
    ASSERT_EQ(p.kind, game::DevPrompt::Kind::Champion);
    const GameState& s = g.session->state();
    ASSERT_EQ(p.objs, std::vector<ObjId>{s.player(g.dev_seat()).champion.front()});
    EXPECT_EQ(s.decision.kind, DecisionKind::Mulligan);
    const auto options = champion_options(g.session->config().decks[g.dev_seat()]);
    ASSERT_EQ(options.size(), 2u);
    const ObjId champ = p.objs[0];
    ASSERT_EQ(s.obj(champ).card, options[0]);
    g.check_truth = false;  // the test names a champion of its own, not SimDev's

    const Card* legend = s.obj(s.player(g.dev_seat()).legend.front()).card;
    const Card* field = s.obj(s.battlefields[1].card).card;
    const Card* unit = nullptr;  // a unit of Dev's deck that isn't a champion
    for (const ObjId id : s.player(g.dev_seat()).main_deck) {
        if (s.obj(id).card->type == CardType::Unit && !s.obj(id).card->champion) unit = s.obj(id).card;
    }
    ASSERT_NE(unit, nullptr);
    const GameState before = s;
    for (const auto& wrong : std::vector<std::vector<const Card*>>{{}, {options[1], options[1]}, {legend}, {field}, {unit}}) {
        EXPECT_THROW(g.session->on_reveal(wrong), game::Refused);
        EXPECT_TRUE(g.session->state() == before);
        EXPECT_EQ(g.session->status(), Status::DevToReveal);
    }
    g.session->on_reveal({options[1]});
    EXPECT_EQ(g.session->state().obj(champ).card, options[1]);
    EXPECT_NE(g.session->replay_text().find("bind " + std::to_string(champ) + " " + options[1]->home().code.str()),
              std::string::npos);
    const auto pool = game::dev_pool(g.session->state(), g.session->buddy(), {}, g.session->config().decks[g.dev_seat()], false);
    EXPECT_NE(std::find(pool.begin(), pool.end(), options[0]), pool.end()) << "the decklist's champion is back in the pool";
    ASSERT_EQ(g.session->status(), Status::DevToReveal);  // then the battlefield
    EXPECT_EQ(g.session->prompt()->kind, game::DevPrompt::Kind::Battlefield);
    EXPECT_TRUE(has_voice(g, "My champion is " + g.session->state().obj(g.session->state().player(0).champion.front()).card->name));
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
            for (const auto& o : list) out.emplace_back(o.action, o.card);
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

// #50: Buddy says one line per action group (a play, activation or move with its choices, never a pass), battlefields
// by name, and every scoring with the score; Dev's own moves aren't voiced. The log has one line per action.
TEST(RiftboundSession, VoiceLinesPerActionGroupAndScoring) {
    std::map<std::string, int> kinds;
    for (int i = 0; i < 4; ++i) {
        const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i % 2)];
        Game g(d0, d1, static_cast<PlayerId>(i / 2), 700 + static_cast<std::uint64_t>(i), {}, 5, true);
        std::set<std::string> fields;
        long buddy_actions = 0, actions_seen = 0;
        int last_turn = 0;
        g.stop_when = [&] {
            const GameState& s = g.session->state();
            for (const auto& b : s.battlefields) fields.insert(s.obj(b.card).card->name);
            const auto n = static_cast<long>(g.session->replay().actions.size());
            if (n != actions_seen) {  // one log line per committed action, the newest last
                const auto& log = g.session->log();
                if (log.size() != std::min<std::size_t>(game::log_lines, static_cast<std::size_t>(n))) {
                    g.problems.push_back("log has " + std::to_string(log.size()) + " lines after " + std::to_string(n) + " actions");
                }
                if (!log.empty() && (log.back().turn < last_turn || log.back().text.find("hidden") != std::string::npos)) {
                    g.problems.push_back("log line: " + log.back().text);
                }
                if (!log.empty()) last_turn = log.back().turn;
                actions_seen = n;
            }
            return false;
        };
        g.buddy_choice = [&](const game::AiRequest&) {
            ++buddy_actions;
            return std::optional<Action>{};
        };
        g.run();
        SCOPED_TRACE("game " + std::to_string(i));
        ASSERT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        long buddy_lines = 0;
        int points_seen = 0;
        for (const auto& line : g.voice) {
            EXPECT_EQ(line.find("hidden"), std::string::npos) << line;
            EXPECT_EQ(line.find("left"), std::string::npos) << line;
            EXPECT_EQ(line.find("right"), std::string::npos) << line;
            const auto names_field = [&] {
                return std::any_of(fields.begin(), fields.end(), [&](const auto& f) { return line.ends_with(f) || line.find(f + ".") != std::string::npos || line.find(f + " with") != std::string::npos; });
            };
            if (line.starts_with("I play ") || line.starts_with("I activate ") || line.starts_with("I equip ") ||
                line.starts_with("I empower ") || line.starts_with("I move ") || line.starts_with("I attack at ")) {
                ++buddy_lines;
                kinds[line.substr(0, line.find(' ', 2))]++;
                if (line.starts_with("I attack at ") || (line.starts_with("I move ") && !line.ends_with("my base"))) {
                    EXPECT_TRUE(names_field()) << line;
                }
                if (line.starts_with("I play ") && line.find(" to ") != std::string::npos) {
                    EXPECT_TRUE(names_field()) << line;
                    ++kinds["play to a battlefield"];
                }
                if (line.find(" on ") != std::string::npos) ++kinds["with a target"];
            } else if (line.find(" conquer ") != std::string::npos || line.find(" hold ") != std::string::npos) {
                EXPECT_TRUE(line.starts_with("I ") || line.starts_with("You ")) << line;
                EXPECT_TRUE(names_field()) << line;
                int a = 0, b = 0;
                const auto dot = line.find(". ");
                ASSERT_NE(dot, std::string::npos) << line;
                ASSERT_EQ(std::sscanf(line.c_str() + dot + 2, "%d to %d.", &a, &b), 2) << line;
                EXPECT_GE(a + b, points_seen) << line;  // the score only grows
                points_seen = a + b;
                ++kinds[line.find(" conquer ") != std::string::npos ? "conquer" : "hold"];
            }
        }
        // never a line per pass: far fewer lines than Buddy's asked decisions, but some
        EXPECT_GT(buddy_lines, 5);
        EXPECT_LT(buddy_lines, buddy_actions);
        EXPECT_TRUE(has_voice(g, " win") || has_voice(g, "concede"));
    }
    for (const auto& [k, n] : kinds) std::cout << k << ": " << n << "\n";
    for (const char* k : {"I play", "I move", "I attack", "play to a battlefield", "conquer", "hold"}) {
        EXPECT_GT(kinds[k], 0) << k;
    }
}

// #49's gate: SimDev (a true shuffled deck, greedy) against Buddy (greedy), both decks from both seats, fault-free
// table. No refusal, no desync, no pause; Dev's options always hold SimDev's true move, the offered cards are exactly
// the possible ones, every card Buddy learns of Dev's comes from a prompt or Dev's choice and is SimDev's true card,
// and the game with Dev's true cards from the start is the same legal game (#73: the same decisions, Dev's discards
// asked in both). 50 games in 5 tests (ctest runs them in
// parallel).
void sim_dev_games(int first) {
    game::SessionStats sum;
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
        sum.dev_declared += st.dev_declared;
        sum.prompts += st.prompts;
    }
    EXPECT_GT(sum.dev_named, 20);
    EXPECT_GT(sum.prompts, 20);
    std::cout << "SimDev games " << first << "–" << first + 9 << ": Dev decisions " << sum.dev_decisions << ", forced "
              << sum.dev_forced << ", named " << sum.dev_named << ", declared discards " << sum.dev_declared << ", prompts " << sum.prompts << "\n";
}

TEST(RiftboundSession, SimDevGames0) { sim_dev_games(0); }
TEST(RiftboundSession, SimDevGames1) { sim_dev_games(10); }
TEST(RiftboundSession, SimDevGames2) { sim_dev_games(20); }
TEST(RiftboundSession, SimDevGames3) { sim_dev_games(30); }
TEST(RiftboundSession, SimDevGames4) { sim_dev_games(40); }

// #73: Dev declares the spell Mel's Empower discards ("… (discard X)"), the Discard choice that follows is answered
// with it, and no discard ever turns into a prompt afterwards (the referee never picks from Dev's placeholders).
TEST(RiftboundSession, DevDeclaresTheSpellACostDiscards) {
    long declared = 0, hand_prompts = 0, texts = 0;
    for (int i = 0; i < 30 && declared < 2; ++i) {
        Game g("vi-unleashed", "zed-vendetta", 0, 1300 + static_cast<std::uint64_t>(i), {}, 13, true);  // Dev: Zed (Mel)
        g.stop_when = [&] {
            const Session& s = *g.session;
            if (s.status() == Status::DevToReveal && s.prompt()->kind == game::DevPrompt::Kind::Hand) ++hand_prompts;
            if (s.status() != Status::DevToAct) return false;
            const GameState pv = public_view(s.state());
            for (const auto& o : s.dev_options()) {
                if (!o.discards()) continue;
                ++texts;
                EXPECT_EQ(o.card->type, CardType::Spell);
                EXPECT_TRUE(game::option_text(pv, o).ends_with("(discard " + o.card->name + ")")) << game::option_text(pv, o);
            }
            return false;
        };
        g.run();
        SCOPED_TRACE("seed " + std::to_string(1300 + i));
        EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_EQ(g.replay_problem(), "");
        EXPECT_EQ(g.true_replay_problem(), "");
        declared += g.session->stats().dev_declared;
    }
    EXPECT_GE(declared, 2);
    EXPECT_GT(texts, declared);
    EXPECT_EQ(hand_prompts, 0);
}
