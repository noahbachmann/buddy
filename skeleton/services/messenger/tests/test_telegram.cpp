#include <gtest/gtest.h>

#include "fake_http.hpp"
#include "telegram.hpp"

using namespace buddy::messenger;
using namespace buddy::test;
using nlohmann::json;

namespace {

const Credentials kDev{.token = "123:SECRET", .user_id = 7, .chat_id = 7};

FakeHttp fake() { return FakeHttp({{"https://api.telegram.org/bot123:SECRET", ""}}); }

Send plain(const std::string& text) { return {.tag = 1, .text = text, .buttons = {}, .force_reply = false, .silent = false}; }

std::string updates(const std::string& list) { return R"({"ok": true, "result": )" + list + "}"; }

// Dev's private chat
std::string message(int update_id, int message_id, const std::string& more) {
    return R"({"update_id": )" + std::to_string(update_id) + R"(, "message": {"message_id": )" + std::to_string(message_id) +
           R"(, "from": {"id": 7, "is_bot": false, "first_name": "Dev"}, "chat": {"id": 7, "type": "private"}, "date": 1, )" +
           more + "}}";
}

struct DeadHttp : buddy::Http {
    buddy::HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>&,
                                const std::string&) override {
        throw buddy::HttpError(method + " " + url + ": Couldn't resolve host name");
    }
};

} // namespace

TEST(Telegram, SendsAMessage) {
    auto http = fake();
    Telegram telegram(kDev.token, http);
    http.on("POST /sendMessage", 200, R"({"ok": true, "result": {"message_id": 55, "chat": {"id": 7}}})");
    EXPECT_EQ(telegram.send(7, plain("Laundry is done")), 55);
    EXPECT_EQ(http.calls[0].url, "https://api.telegram.org/bot123:SECRET/sendMessage");
    EXPECT_EQ(json::parse(http.calls[0].body),
              (json{{"chat_id", 7}, {"text", "Laundry is done"}, {"link_preview_options", {{"is_disabled", true}}}}));

    Send quiet = plain("fyi");
    quiet.silent = true;
    telegram.send(7, quiet);
    EXPECT_EQ(json::parse(http.calls[1].body)["disable_notification"], true);
}

TEST(Telegram, QuestionsCarryButtonsOrOpenTheReplyField) {
    auto http = fake();
    Telegram telegram(kDev.token, http);
    http.on("POST /sendMessage", 200, R"({"ok": true, "result": {"message_id": 56}})");
    Send buttons = plain("Tea or coffee?");
    buttons.buttons = {{"tea", "q1:0"}, {"coffee", "q1:1"}};
    buttons.force_reply = true;  // buttons win: one reply_markup per message
    telegram.send(7, buttons);
    EXPECT_EQ(json::parse(http.calls[0].body)["reply_markup"],
              json::parse(R"({"inline_keyboard": [[{"text": "tea", "callback_data": "q1:0"}],
                                                  [{"text": "coffee", "callback_data": "q1:1"}]]})"));
    Send open = plain("When?");
    open.force_reply = true;
    telegram.send(7, open);
    EXPECT_EQ(json::parse(http.calls[1].body)["reply_markup"]["force_reply"], true);
}

TEST(Telegram, EditsAndTakesTheButtonsOff) {
    auto http = fake();
    Telegram telegram(kDev.token, http);
    http.on("POST /editMessageText", 200, R"({"ok": true, "result": {"message_id": 56}})");
    telegram.edit(7, {.message_id = 56, .text = "Tea or coffee?\n\n✓ tea"});
    const json body = json::parse(http.calls[0].body);
    EXPECT_EQ(body["chat_id"], 7);
    EXPECT_EQ(body["message_id"], 56);
    EXPECT_EQ(body["text"], "Tea or coffee?\n\n✓ tea");
    EXPECT_EQ(body["reply_markup"], json::parse(R"({"inline_keyboard": []})"));

    http.on("POST /editMessageText", 400,
            R"({"ok": false, "error_code": 400, "description": "Bad Request: message is not modified: specified new message content and reply markup are exactly the same"})");
    EXPECT_NO_THROW(telegram.edit(7, {.message_id = 56, .text = "same"}));
    http.on("POST /editMessageText", 400, R"({"ok": false, "error_code": 400, "description": "Bad Request: message to edit not found"})");
    EXPECT_THROW(telegram.edit(7, {.message_id = 1, .text = "gone"}), ApiError);
}

TEST(Telegram, SmallCalls) {
    auto http = fake();
    Telegram telegram(kDev.token, http);
    http.on("POST /answerCallbackQuery", 200, R"({"ok": true, "result": true})");
    telegram.answer_callback({.callback_id = "cb1", .text = ""});
    EXPECT_EQ(json::parse(http.calls[0].body), (json{{"callback_query_id", "cb1"}}));
    telegram.answer_callback({.callback_id = "cb2", .text = "That question is closed."});
    EXPECT_EQ(json::parse(http.calls[1].body)["text"], "That question is closed.");

    http.on("POST /getMe", 200, R"({"ok": true, "result": {"id": 999, "is_bot": true, "first_name": "Buddy", "username": "my_buddy_bot"}})");
    const auto me = telegram.get_me();
    EXPECT_EQ(me.id, 999);
    EXPECT_EQ(me.username, "my_buddy_bot");

    http.on("POST /setMyCommands", 200, R"({"ok": true, "result": true})");
    telegram.set_commands();
    const json commands = json::parse(http.calls.back().body)["commands"];
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0]["command"], "status");
    EXPECT_EQ(commands[1]["command"], "open");
}

TEST(Telegram, ErrorsSayWhatToDo) {
    auto http = fake();
    Telegram telegram(kDev.token, http);
    auto failure = [&](long status, const std::string& body) {
        http.on("POST /sendMessage", status, body);
        try {
            telegram.send(7, plain("x"));
        } catch (const ApiError& e) {
            return std::pair{classify(e), e.retry_after};
        }
        ADD_FAILURE() << "no error for " << status;
        return std::pair{Failure::Refused, 0};
    };
    EXPECT_EQ(failure(401, R"({"ok": false, "error_code": 401, "description": "Unauthorized"})").first, Failure::Unauthorized);
    EXPECT_EQ(failure(404, R"({"ok": false, "error_code": 404, "description": "Not Found"})").first, Failure::Unauthorized);
    EXPECT_EQ(failure(403, R"({"ok": false, "error_code": 403, "description": "Forbidden: bot was blocked by the user"})").first,
              Failure::Blocked);
    EXPECT_EQ(failure(400, R"({"ok": false, "error_code": 400, "description": "Bad Request: chat not found"})").first,
              Failure::Refused);
    EXPECT_EQ(failure(429, R"({"ok": false, "error_code": 429, "description": "Too Many Requests: retry after 17",
                               "parameters": {"retry_after": 17}})"),
              (std::pair{Failure::Retry, 17}));
    EXPECT_EQ(failure(502, "<html>Bad Gateway</html>").first, Failure::Retry);
    EXPECT_EQ(failure(200, "<html>Sign in to this Wi-Fi</html>").first, Failure::Retry);  // captive portal
    EXPECT_EQ(failure(200, R"({"ok": true, "result": {}})").first, Failure::Retry);       // no message_id

    try {
        http.on("POST /sendMessage", 400, R"({"ok": false, "error_code": 400, "description": "Bad Request: chat not found"})");
        telegram.send(7, plain("x"));
        FAIL();
    } catch (const ApiError& e) {
        EXPECT_STREQ(e.what(), "Bad Request: chat not found");
        EXPECT_EQ(e.status, 400);
    }
}

TEST(Telegram, TokenNeverShowsInErrors) {
    DeadHttp http;
    Telegram telegram(kDev.token, http);
    try {
        telegram.send(7, plain("x"));
        FAIL();
    } catch (const buddy::HttpError& e) {
        EXPECT_STREQ(e.what(), "POST https://api.telegram.org/bot<token>/sendMessage: Couldn't resolve host name");
    }
    EXPECT_EQ(telegram.redact("GET " + telegram.updates_url(5, 50) + ": Timeout was reached").find("SECRET"), std::string::npos);
}

TEST(Telegram, UpdatesUrl) {
    auto http = fake();
    const Telegram telegram(kDev.token, http);
    EXPECT_EQ(telegram.updates_url(0, 50),
              "https://api.telegram.org/bot123:SECRET/getUpdates?timeout=50&allowed_updates=%5B%22message%22%2C%22callback_query%22%5D");
    EXPECT_TRUE(telegram.updates_url(8001, 0).ends_with("&offset=8001"));
}

TEST(Updates, MessagesRepliesAndTaps) {
    const auto got = parse_updates(
        200, updates("[" + message(8000, 60, R"("text": "around 7")") + "," +
                     message(8001, 61, R"("text": "tea", "reply_to_message": {"message_id": 56, "chat": {"id": 7}})") + "," +
                     R"({"update_id": 8002, "callback_query": {"id": "cb9", "from": {"id": 7, "is_bot": false},
                         "message": {"message_id": 56, "chat": {"id": 7, "type": "private"}, "date": 1, "text": "Tea?"},
                         "chat_instance": "x", "data": "q1:0"}})" +
                     "]"));
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0].update_id, 8000);
    EXPECT_TRUE(got[0].from(kDev));
    EXPECT_EQ(got[0].in.message_id, 60);
    EXPECT_EQ(got[0].in.text, "around 7");
    EXPECT_EQ(got[0].in.reply_to, 0);
    EXPECT_EQ(got[0].in.callback_id, "");

    EXPECT_EQ(got[1].in.reply_to, 56);

    EXPECT_TRUE(got[2].from(kDev));
    EXPECT_EQ(got[2].in.callback_id, "cb9");
    EXPECT_EQ(got[2].in.data, "q1:0");
    EXPECT_EQ(got[2].in.message_id, 56);
    EXPECT_EQ(got[2].in.text, "");  // the button's message text is not Dev's
}

TEST(Updates, OnlyDevInThePairedChatCounts) {
    const auto got = parse_updates(200, updates(R"([
        {"update_id": 1, "message": {"message_id": 1, "from": {"id": 8, "is_bot": false}, "chat": {"id": 8, "type": "private"}, "text": "hi"}},
        {"update_id": 2, "message": {"message_id": 2, "from": {"id": 7, "is_bot": false}, "chat": {"id": -100, "type": "group"}, "text": "hi"}},
        {"update_id": 3, "message": {"message_id": 3, "from": {"id": 7, "is_bot": false}, "chat": {"id": 9, "type": "private"}, "text": "hi"}},
        {"update_id": 4, "callback_query": {"id": "cb", "from": {"id": 8, "is_bot": false},
                                            "message": {"message_id": 56, "chat": {"id": 7, "type": "private"}}, "data": "q1:0"}},
        {"update_id": 5, "edited_message": {"message_id": 1, "from": {"id": 7}, "chat": {"id": 7, "type": "private"}, "text": "x"}},
        {"update_id": 6, "message": {"message_id": 4, "chat": {"id": 7, "type": "private"}, "text": "no sender"}},
        {"no": "update_id"}
    ])"));
    ASSERT_EQ(got.size(), 6u);  // all kept: their update_ids still have to be confirmed
    for (const auto& u : got) EXPECT_FALSE(u.from(kDev)) << u.update_id;
    EXPECT_EQ(got.back().update_id, 6);
}

TEST(Updates, Errors) {
    EXPECT_TRUE(parse_updates(200, updates("[]")).empty());
    try {
        parse_updates(409, R"({"ok": false, "error_code": 409, "description": "Conflict: terminated by other getUpdates request"})");
        FAIL();
    } catch (const ApiError& e) {
        EXPECT_EQ(classify(e), Failure::Conflict);
    }
    EXPECT_THROW(parse_updates(200, ""), ApiError);  // poll cut off
    EXPECT_THROW(parse_updates(200, R"({"ok": true, "result": {}})"), ApiError);
    EXPECT_THROW(parse_updates(502, "Bad Gateway"), ApiError);
}

TEST(Updates, PairingCode) {
    const auto got = parse_updates(200, updates(R"([
        {"update_id": 1, "message": {"message_id": 1, "from": {"id": 7, "is_bot": false}, "chat": {"id": 7, "type": "private"}, "text": "/start 482913"}},
        {"update_id": 2, "message": {"message_id": 2, "from": {"id": 7, "is_bot": false}, "chat": {"id": 7, "type": "private"}, "text": " 482913 "}},
        {"update_id": 3, "message": {"message_id": 3, "from": {"id": 7, "is_bot": false}, "chat": {"id": 7, "type": "private"}, "text": "/start"}},
        {"update_id": 4, "message": {"message_id": 4, "from": {"id": 7, "is_bot": false}, "chat": {"id": -5, "type": "group"}, "text": "482913"}},
        {"update_id": 5, "message": {"message_id": 5, "from": {"id": 9, "is_bot": true}, "chat": {"id": 9, "type": "private"}, "text": "482913"}},
        {"update_id": 6, "message": {"message_id": 6, "from": {"id": 7, "is_bot": false}, "chat": {"id": 7, "type": "private"}, "text": "my code is 482913"}}
    ])"));
    ASSERT_EQ(got.size(), 6u);
    EXPECT_TRUE(got[0].carries_code("482913"));
    EXPECT_TRUE(got[1].carries_code("482913"));
    EXPECT_FALSE(got[2].carries_code("482913"));
    EXPECT_FALSE(got[3].carries_code("482913"));
    EXPECT_FALSE(got[4].carries_code("482913"));
    EXPECT_FALSE(got[5].carries_code("482913"));
    EXPECT_FALSE(got[0].carries_code(""));
}

TEST(Credentials, FileRoundTrip) {
    const auto path = (temp_dir() / "messenger.json").string();
    EXPECT_EQ(load_credentials(path), std::nullopt);
    save_credentials(path, kDev);
    const auto loaded = load_credentials(path);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->token, "123:SECRET");
    EXPECT_EQ(loaded->user_id, 7);
    EXPECT_EQ(loaded->chat_id, 7);
    write(path, R"({"token": "123:SECRET"})");
    EXPECT_THROW(load_credentials(path), std::runtime_error);
}
