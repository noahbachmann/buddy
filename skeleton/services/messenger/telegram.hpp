// Telegram Bot API (https://core.telegram.org/bots/api), the few methods the messenger needs, on the Http interface.
// Outbound HTTPS only: messages go out with sendMessage, Dev's side comes in by long-polling getUpdates. No webhook,
// no open port.
//
// The bot token is part of every URL. It never leaves this file in an error text: HttpError messages (which
// carry the URL) are rethrown redacted.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "buddy/http.hpp"
#include "chat.hpp"

namespace buddy::messenger {

// ~/buddy/config/messenger.json, written by --pair
struct Credentials {
    std::string token;
    std::int64_t user_id = 0;  // Dev's account: the only one the bot listens to
    std::int64_t chat_id = 0;  // the private chat with Dev

    bool operator==(const Credentials&) const = default;
};

// nullopt: no file yet (not paired). Throws std::runtime_error when the file is there but unusable.
std::optional<Credentials> load_credentials(const std::string& path);
void save_credentials(const std::string& path, const Credentials& c);

// Telegram answered, and said no.
struct ApiError : std::runtime_error {
    ApiError(long status_, const std::string& what, int retry_after_ = 0)
        : std::runtime_error(what), status(status_), retry_after(retry_after_) {}
    long status;      // Telegram's error_code, else the HTTP status
    int retry_after;  // s, with a 429
};

// What an ApiError means for the message that caused it
enum class Failure {
    Retry,         // 429, 5xx: later, same message
    Unauthorized,  // token revoked or wrong: nothing works until --pair
    Blocked,       // Dev blocked the bot or deleted the chat
    Conflict,      // getUpdates: someone else is polling with this token
    Refused,       // this message is the problem: drop it
};
Failure classify(const ApiError& e);

// One entry of getUpdates. `in` is filled for messages and button taps; other kinds only carry the update_id.
struct Update {
    std::int64_t update_id = 0;
    std::int64_t user_id = 0;
    std::int64_t chat_id = 0;
    bool private_chat = false;
    bool from_bot = false;
    Incoming in;

    // From Dev, in the paired chat. Everything else is dropped without an answer.
    bool from(const Credentials& c) const { return private_chat && user_id == c.user_id && chat_id == c.chat_id; }
    // Pairing: a person sent this code to the bot in a private chat ("123456" or "/start 123456").
    bool carries_code(const std::string& code) const;
};

// The response of a getUpdates call. Throws ApiError when Telegram refused or the body isn't the API's.
std::vector<Update> parse_updates(long status, const std::string& body);

class Telegram {
public:
    Telegram(std::string token, Http& http);

    struct Me {
        std::int64_t id = 0;
        std::string username;
    };
    Me get_me();

    // All throw ApiError (Telegram said no) or HttpError (not reached; token redacted).
    std::int64_t send(std::int64_t chat_id, const Send& message);  // → message_id
    void edit(std::int64_t chat_id, const Edit& edit);             // unchanged text counts as done
    void answer_callback(const AnswerCallback& answer);
    void set_commands();  // the "/" menu: /status, /open

    // For the long poll (CurlHttp::stream): waits up to timeout_s for something new. offset = last update_id + 1
    // confirms everything before it; 0 = whatever is waiting.
    std::string updates_url(std::int64_t offset, int timeout_s) const;

    // text with the token replaced: for anything that may carry a URL
    std::string redact(std::string text) const;

private:
    nlohmann::json call(const std::string& method, const nlohmann::json& params);

    std::string token_;
    Http& http_;
};

} // namespace buddy::messenger
