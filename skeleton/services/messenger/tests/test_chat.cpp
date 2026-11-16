#include <gtest/gtest.h>

#include "chat.hpp"

using namespace buddy::messenger;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

const Time t0 = Time{} + std::chrono::hours(500000);

Ask ask(const std::string& id, const std::string& text, std::vector<std::string> options = {}) {
    return {.id = id, .text = text, .options = std::move(options), .from = "", .ttl_s = std::nullopt};
}

Incoming message(std::int64_t id, const std::string& text, std::int64_t reply_to = 0) {
    return {.message_id = id, .reply_to = reply_to, .text = text, .callback_id = "", .data = ""};
}

Incoming tap(const std::string& data, std::int64_t on_message) {
    return {.message_id = on_message, .reply_to = 0, .text = "", .callback_id = "cb", .data = data};
}

// Plays main.cpp: delivers the whole outbox, numbering sent messages from `next_id`. Returns what went out.
struct Phone {
    std::vector<Send> sends;
    std::vector<Edit> edits;
    std::vector<AnswerCallback> toasts;
    std::int64_t next_id = 100;

    void deliver(Chat& chat) {
        sends.clear(), edits.clear(), toasts.clear();
        while (!chat.outbox().empty()) {
            const Out out = chat.outbox().front();
            chat.outbox().pop_front();
            if (const auto* s = std::get_if<Send>(&out)) {
                sends.push_back(*s);
                chat.sent(s->tag, next_id++);
            } else if (const auto* e = std::get_if<Edit>(&out)) {
                edits.push_back(*e);
            } else {
                toasts.push_back(std::get<AnswerCallback>(out));
            }
        }
    }
};

Event answered(const std::string& id, const std::string& text, int option = -1) {
    return {.event = "answered", .id = id, .text = text, .option = option, .reason = ""};
}
Event plain(const std::string& event, const std::string& id, const std::string& reason = "") {
    return {.event = event, .id = id, .text = "", .option = -1, .reason = reason};
}

} // namespace

TEST(Chat, QuestionWithButtons) {
    Chat chat;
    Phone phone;
    chat.command(Ask{.id = "t1", .text = "Tea or coffee?", .options = {"tea", "coffee"}, .from = "brain", .ttl_s = {}}, t0);
    EXPECT_TRUE(chat.take_events().empty());  // not asked before it is in the chat
    EXPECT_TRUE(chat.take_dirty());
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_EQ(phone.sends[0].text, "❓ brain: Tea or coffee?");
    EXPECT_EQ(phone.sends[0].buttons, (std::vector<Button>{{"tea", "q1:0"}, {"coffee", "q1:1"}}));
    EXPECT_FALSE(phone.sends[0].force_reply);
    EXPECT_FALSE(phone.sends[0].silent);
    EXPECT_EQ(chat.take_events(), std::vector{plain("asked", "t1")});
    EXPECT_EQ(chat.open(), 1u);

    chat.incoming(tap("q1:1", 100), t0 + 5s);
    EXPECT_EQ(chat.take_events(), std::vector{answered("t1", "coffee", 1)});
    EXPECT_EQ(chat.open(), 0u);
    EXPECT_TRUE(chat.take_dirty());
    phone.deliver(chat);
    ASSERT_EQ(phone.toasts.size(), 1u);
    EXPECT_EQ(phone.toasts[0].callback_id, "cb");
    EXPECT_EQ(phone.toasts[0].text, "");
    ASSERT_EQ(phone.edits.size(), 1u);
    EXPECT_EQ(phone.edits[0].message_id, 100);
    EXPECT_EQ(phone.edits[0].text, "❓ brain: Tea or coffee?\n\n✓ coffee");
}

TEST(Chat, FreeTextQuestionTakesAnyMessage) {
    Chat chat;
    Phone phone;
    chat.command(ask("t1", "When are you home?"), t0);
    phone.deliver(chat);
    EXPECT_TRUE(phone.sends[0].force_reply);
    EXPECT_TRUE(phone.sends[0].buttons.empty());
    chat.take_events();

    chat.incoming(message(101, "  around 7 "), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("t1", "around 7")});
    phone.deliver(chat);
    EXPECT_EQ(phone.edits[0].text, "❓ When are you home?\n\n✓ around 7");
    EXPECT_EQ(answered("t1", "around 7").to_json(), (json{{"event", "answered"}, {"id", "t1"}, {"text", "around 7"}}));
    EXPECT_EQ(answered("t1", "tea", 0).to_json()["option"], 0);
}

TEST(Chat, TypedOptionCountsAsThatOption) {
    Chat chat;
    Phone phone;
    chat.command(ask("t1", "Tea or coffee?", {"tea", "coffee"}), t0);
    phone.deliver(chat);
    chat.take_events();
    chat.incoming(message(101, "Coffee"), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("t1", "coffee", 1)});

    chat.command(ask("t2", "Tea or coffee?", {"tea", "coffee"}), t0);
    phone.deliver(chat);
    chat.take_events();
    chat.incoming(message(103, "water"), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("t2", "water")});
}

TEST(Chat, RepeatedAskNeverAsksTwice) {
    Chat chat;
    Phone phone;
    chat.command(ask("t1", "Sure?", {"yes", "no"}), t0);
    chat.command(ask("t1", "Sure?", {"yes", "no"}), t0);  // messenger not through to Telegram yet
    EXPECT_TRUE(chat.take_events().empty());
    phone.deliver(chat);
    EXPECT_EQ(phone.sends.size(), 1u);
    chat.take_events();

    chat.command(ask("t1", "Sure?", {"yes", "no"}), t0);  // the asker restarted
    EXPECT_EQ(chat.take_events(), std::vector{plain("asked", "t1")});
    chat.incoming(tap("q1:0", 100), t0);
    chat.take_events();
    chat.command(ask("t1", "Sure?", {"yes", "no"}), t0);  // it missed the answer
    EXPECT_EQ(chat.take_events(), std::vector{answered("t1", "yes", 0)});
    phone.deliver(chat);
    EXPECT_TRUE(phone.sends.empty());

    // a day later the id is free again
    chat.tick(t0 + 25h);
    chat.command(ask("t1", "Sure?", {"yes", "no"}), t0 + 25h);
    phone.deliver(chat);
    EXPECT_EQ(phone.sends.size(), 1u);
}

TEST(Chat, ReplyPicksItsQuestion) {
    Chat chat;
    Phone phone;
    chat.command(ask("a", "First?"), t0);
    chat.command(ask("b", "Second?"), t0);
    phone.deliver(chat);  // messages 100, 101
    chat.take_events();
    chat.incoming(message(102, "for the second", 101), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("b", "for the second")});
    chat.incoming(message(103, "again", 101), t0);
    EXPECT_TRUE(chat.take_events().empty());
    phone.deliver(chat);
    EXPECT_EQ(phone.sends.back().text, "That question is closed.");
    EXPECT_TRUE(phone.sends.back().silent);
}

TEST(Chat, PlainMessageWithSeveralOpenAsksWhich) {
    Chat chat;
    Phone phone;
    chat.command(ask("a", "First?"), t0);
    chat.command(ask("b", "Second?"), t0);
    phone.deliver(chat);
    chat.take_events();

    chat.incoming(message(102, "yes please"), t0);
    EXPECT_TRUE(chat.take_events().empty());
    phone.deliver(chat);  // the prompt is message 102 in this fake's numbering
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_EQ(phone.sends[0].text, "Which question is that for?");
    EXPECT_EQ(phone.sends[0].buttons, (std::vector<Button>{{"First?", "p1:1"}, {"Second?", "p1:2"}}));

    // a newer text replaces the pending one: the old prompt's buttons are void
    chat.incoming(message(104, "no thanks"), t0);
    phone.deliver(chat);
    chat.incoming(tap("p1:2", 102), t0);
    EXPECT_TRUE(chat.take_events().empty());
    phone.deliver(chat);
    EXPECT_EQ(phone.toasts[0].text, "Too late for that one, send it again.");
    EXPECT_EQ(phone.edits[0].text, "(no longer valid)");

    chat.incoming(tap("p2:2", 103), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("b", "no thanks")});
    phone.deliver(chat);
    ASSERT_EQ(phone.edits.size(), 2u);
    EXPECT_EQ(phone.edits[0].message_id, 103);
    EXPECT_EQ(phone.edits[0].text, "→ Second?");
    EXPECT_EQ(phone.edits[1].text, "❓ Second?\n\n✓ no thanks");

    // one left: plain text goes straight to it
    chat.incoming(message(106, "ok"), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("a", "ok")});
}

TEST(Chat, NothingOpen) {
    Chat chat;
    Phone phone;
    chat.incoming(message(5, "hello"), t0);
    chat.incoming(message(6, ""), t0);  // a photo
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 2u);
    EXPECT_EQ(phone.sends[0].text, "Nothing is waiting for an answer.");
    EXPECT_EQ(phone.sends[1].text, "I only read text.");
    EXPECT_TRUE(chat.take_events().empty());
    EXPECT_FALSE(chat.take_dirty());
}

TEST(Chat, Expiry) {
    Chat chat({.default_ttl = 1h});
    Phone phone;
    chat.command(ask("a", "Default?"), t0);
    chat.command(Ask{.id = "b", .text = "Short?", .options = {"y"}, .from = "", .ttl_s = 60}, t0);
    chat.command(Ask{.id = "c", .text = "Forever?", .options = {}, .from = "", .ttl_s = 0}, t0);
    phone.deliver(chat);
    chat.take_events();
    chat.take_dirty();
    EXPECT_EQ(chat.deadline(), t0 + 60s);
    chat.tick(t0 + 59s);
    EXPECT_TRUE(chat.take_events().empty());

    chat.tick(t0 + 60s);
    EXPECT_EQ(chat.take_events(), std::vector{plain("expired", "b")});
    EXPECT_TRUE(chat.take_dirty());
    phone.deliver(chat);
    EXPECT_EQ(phone.edits[0].text, "❓ Short?\n\n⌛ no answer in time");
    EXPECT_EQ(chat.deadline(), t0 + 1h);

    chat.incoming(tap("q2:0", 101), t0 + 61s);  // too late
    EXPECT_TRUE(chat.take_events().empty());
    phone.deliver(chat);
    EXPECT_EQ(phone.toasts[0].text, "That question is closed.");

    chat.tick(t0 + 1h);
    EXPECT_EQ(chat.take_events(), std::vector{plain("expired", "a")});
    EXPECT_EQ(chat.deadline(), std::nullopt);
    EXPECT_EQ(chat.open(), 1u);
}

TEST(Chat, ExpiresUnsentQuestionWithoutSendingIt) {
    Chat chat;
    Phone phone;
    chat.command(Ask{.id = "a", .text = "Quick?", .options = {}, .from = "", .ttl_s = 10}, t0);  // Telegram unreachable
    chat.tick(t0 + 10s);
    EXPECT_EQ(chat.take_events(), std::vector{plain("expired", "a")});
    phone.deliver(chat);
    EXPECT_TRUE(phone.sends.empty());
    EXPECT_TRUE(phone.edits.empty());
}

TEST(Chat, Cancel) {
    Chat chat;
    Phone phone;
    chat.command(ask("a", "Sent?", {"y"}), t0);
    phone.deliver(chat);
    chat.command(ask("b", "Queued?"), t0);
    chat.take_events();

    chat.command(Cancel{"b"}, t0);
    chat.command(Cancel{"a"}, t0);
    chat.command(Cancel{"unknown"}, t0);
    phone.deliver(chat);
    EXPECT_TRUE(phone.sends.empty());
    ASSERT_EQ(phone.edits.size(), 1u);
    EXPECT_EQ(phone.edits[0].text, "❓ Sent?\n\n✗ withdrawn");
    EXPECT_TRUE(chat.take_events().empty());
    EXPECT_EQ(chat.open(), 0u);

    chat.incoming(tap("q1:0", 100), t0);  // the edit hasn't reached the phone yet
    EXPECT_TRUE(chat.take_events().empty());
}

TEST(Chat, MessageTelegramRefusesFailsTheQuestion) {
    Chat chat;
    chat.command(ask("a", "Bad?"), t0);
    const auto tag = std::get<Send>(chat.outbox().front()).tag;
    chat.outbox().pop_front();
    chat.failed(tag, "Bad Request: chat not found");
    EXPECT_EQ(chat.take_events(), std::vector{plain("failed", "a", "Bad Request: chat not found")});
    EXPECT_EQ(chat.open(), 0u);

    chat.command(ask("a", "Bad?"), t0);  // forgotten: the asker may try again
    EXPECT_EQ(chat.outbox().size(), 1u);
}

TEST(Chat, TooManyOpenQuestions) {
    Chat chat({.max_open = 2});
    chat.command(ask("a", "1?"), t0);
    chat.command(ask("b", "2?"), t0);
    chat.command(ask("c", "3?"), t0);
    EXPECT_EQ(chat.take_events(), std::vector{plain("failed", "c", "too many open questions")});
    EXPECT_EQ(chat.outbox().size(), 2u);
}

TEST(Chat, SurvivesARestart) {
    Chat before;
    Phone phone;
    before.command(ask("sent", "In the chat?", {"yes", "no"}), t0);
    before.command(ask("done", "Answered?"), t0);
    phone.deliver(before);  // 100, 101
    before.incoming(message(102, "sure", 101), t0);
    before.command(Ask{.id = "queued", .text = "Not out yet?", .options = {}, .from = "", .ttl_s = 600}, t0);
    before.command(Ask{.id = "old", .text = "Runs out while down?", .options = {}, .from = "", .ttl_s = 30}, t0);
    const json saved = json::parse(before.dump().dump());

    Chat after;
    after.load(saved, t0 + 60s);
    EXPECT_EQ(after.take_events(), std::vector{plain("expired", "old")});
    EXPECT_EQ(after.open(), 2u);
    phone.next_id = 110;
    phone.deliver(after);
    ASSERT_EQ(phone.sends.size(), 1u);  // only the one that never went out
    EXPECT_EQ(phone.sends[0].text, "❓ Not out yet?");
    EXPECT_EQ(after.take_events(), std::vector{plain("asked", "queued")});

    after.incoming(message(102, "sure", 101), t0 + 61s);  // Telegram resends what the crash left unconfirmed
    phone.deliver(after);
    EXPECT_TRUE(phone.sends.empty());
    after.incoming(tap("q1:1", 100), t0 + 62s);
    EXPECT_EQ(after.take_events(), std::vector{answered("sent", "no", 1)});
    after.command(ask("done", "Answered?"), t0 + 63s);
    EXPECT_EQ(after.take_events(), std::vector{answered("done", "sure")});

    // numbers go on: an old button never hits a new question
    after.command(ask("new", "Fresh?", {"x"}), t0 + 64s);
    EXPECT_EQ(std::get<Send>(after.outbox().back()).buttons[0].data, "q5:0");
}

TEST(Chat, LoadSkipsWhatItCantRead) {
    Chat chat;
    chat.load(json::parse(R"({"next_n": "x", "questions": [{"id": "broken"}, 7]})", nullptr, false), t0);
    chat.load(json::array(), t0);
    chat.load(nullptr, t0);
    EXPECT_EQ(chat.open(), 0u);
}

TEST(Chat, StaleButtonGetsItsMessageFixed) {
    Chat chat;
    Phone phone;
    chat.command(ask("a", "Sure?", {"yes", "no"}), t0);
    phone.deliver(chat);
    chat.incoming(tap("q1:0", 100), t0);
    chat.outbox().clear();  // the closing edit is lost in a restart
    chat.incoming(tap("q1:1", 100), t0);
    phone.deliver(chat);
    EXPECT_EQ(phone.toasts[0].text, "That question is closed.");
    ASSERT_EQ(phone.edits.size(), 1u);
    EXPECT_EQ(phone.edits[0].text, "❓ Sure?\n\n✓ yes");
    EXPECT_EQ(chat.take_events().size(), 2u);  // asked, answered: the second tap changed nothing

    for (const char* junk : {"", "q", "q1", "q1:", "q1:9", "q9:0", "qx:y", "p1:1", "z1:1", "q1:-1"}) {
        chat.incoming(tap(junk, 100), t0);
        EXPECT_TRUE(chat.take_events().empty()) << junk;
    }
}

TEST(Chat, Notices) {
    Chat chat;
    Phone phone;
    chat.command(Notify{.text = "Laundry is done", .alert = true, .key = ""}, t0);
    chat.command(Notify{.text = "fyi", .alert = false, .key = ""}, t0);
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 2u);
    EXPECT_EQ(phone.sends[0].text, "Laundry is done");
    EXPECT_FALSE(phone.sends[0].silent);
    EXPECT_TRUE(phone.sends[1].silent);
    EXPECT_TRUE(chat.take_events().empty());
    EXPECT_FALSE(chat.take_dirty());
}

TEST(Chat, KeyedNoticeIsRewrittenInPlace) {
    Chat chat;
    Phone phone;
    chat.command(Notify{.text = "hue is offline", .alert = true, .key = "watch:hue"}, t0);
    phone.deliver(chat);  // 100
    chat.command(Notify{.text = "hue is back", .alert = false, .key = "watch:hue"}, t0);
    phone.deliver(chat);
    EXPECT_TRUE(phone.sends.empty());
    ASSERT_EQ(phone.edits.size(), 1u);
    EXPECT_EQ(phone.edits[0].message_id, 100);
    EXPECT_EQ(phone.edits[0].text, "hue is back");

    // the next outage rings again and takes over the key
    chat.command(Notify{.text = "hue is offline", .alert = true, .key = "watch:hue"}, t0);
    phone.deliver(chat);  // 101
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_FALSE(phone.sends[0].silent);
    chat.command(Notify{.text = "hue is back", .alert = false, .key = "watch:hue"}, t0);
    phone.deliver(chat);
    EXPECT_EQ(phone.edits[0].message_id, 101);

    // info under a key nobody has seen: a quiet new message
    chat.command(Notify{.text = "web is back", .alert = false, .key = "watch:web"}, t0);
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_TRUE(phone.sends[0].silent);
}

TEST(Chat, KeyedNoticeStillQueuedGetsTheNewestWording) {
    Chat chat;
    Phone phone;
    chat.command(Notify{.text = "hue is offline", .alert = true, .key = "watch:hue"}, t0);  // Telegram unreachable
    chat.command(Notify{.text = "hue is back", .alert = false, .key = "watch:hue"}, t0);
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_EQ(phone.sends[0].text, "hue is back");
    EXPECT_FALSE(phone.sends[0].silent);  // it was an outage: still rings
    EXPECT_TRUE(phone.edits.empty());
}

TEST(Chat, FullQueueDropsNotices) {
    Chat chat({.max_queued = 2});
    EXPECT_TRUE(chat.command(Notify{.text = "1", .alert = true, .key = ""}, t0));
    EXPECT_TRUE(chat.command(Notify{.text = "2", .alert = true, .key = ""}, t0));
    EXPECT_FALSE(chat.command(Notify{.text = "3", .alert = true, .key = ""}, t0));
    EXPECT_TRUE(chat.command(ask("a", "Questions still go in?"), t0));
    EXPECT_EQ(chat.outbox().size(), 3u);
}

TEST(Chat, StatusQuestionPhrases) {
    EXPECT_TRUE(is_status_question("status"));
    EXPECT_TRUE(is_status_question("Status?"));
    EXPECT_TRUE(is_status_question("What's the status?"));
    EXPECT_TRUE(is_status_question("what is the status"));
    EXPECT_TRUE(is_status_question("What’s the status"));  // Telegram's apostrophe
    EXPECT_TRUE(is_status_question("How's the status?"));
    EXPECT_FALSE(is_status_question("the status of what"));
    EXPECT_FALSE(is_status_question("tea"));
    EXPECT_FALSE(is_status_question("done with the status page"));
}

TEST(Chat, PlainTextStatusBeatsAnswering) {
    Chat chat({}, [] { return std::string("🟢 buddy · CPU 3%\nAll 11 services online."); });
    Phone phone;
    chat.command(ask("a", "Sure?", {"yes"}), t0);
    phone.deliver(chat);  // 101
    chat.take_events();

    chat.incoming(message(1, "What's the status?"), t0);
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_EQ(phone.sends[0].text, "🟢 buddy · CPU 3%\nAll 11 services online.\n1 open question.");
    EXPECT_EQ(chat.open(), 1u);  // not taken as the answer

    chat.incoming(message(2, "yes"), t0);  // other text still answers
    EXPECT_EQ(chat.open(), 0u);
}

TEST(Chat, BotCommands) {
    Chat chat({}, [] { return std::string("All 9 services online."); });
    Phone phone;
    chat.incoming(message(1, "/status"), t0);
    chat.incoming(message(2, "/open"), t0);
    chat.incoming(message(3, "/start 123456"), t0);
    phone.deliver(chat);
    ASSERT_EQ(phone.sends.size(), 3u);
    EXPECT_EQ(phone.sends[0].text, "All 9 services online.\nNo open questions.");
    EXPECT_EQ(phone.sends[1].text, "No open questions.");
    EXPECT_TRUE(phone.sends[2].text.starts_with("I write when something needs you"));

    chat.command(ask("a", "Sure?", {"yes"}), t0);
    phone.deliver(chat);  // 103
    chat.take_events();
    chat.incoming(message(104, "/status@buddy_bot"), t0);
    phone.deliver(chat);  // 104
    EXPECT_EQ(phone.sends[0].text, "All 9 services online.\n1 open question.");
    EXPECT_EQ(chat.open(), 1u);  // a command is no answer

    chat.incoming(message(106, "/open"), t0);
    phone.deliver(chat);  // the question again, as 105
    ASSERT_EQ(phone.edits.size(), 1u);
    EXPECT_EQ(phone.edits[0].message_id, 103);
    EXPECT_EQ(phone.edits[0].text, "❓ Sure?\n\n↓ asked again below");
    ASSERT_EQ(phone.sends.size(), 1u);
    EXPECT_EQ(phone.sends[0].buttons[0].data, "q1:0");
    EXPECT_EQ(chat.take_events(), std::vector{plain("asked", "a")});
    chat.incoming(tap("q1:0", 105), t0);
    EXPECT_EQ(chat.take_events(), std::vector{answered("a", "yes", 0)});
}
