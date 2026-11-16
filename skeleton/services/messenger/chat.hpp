// The conversation with Dev, as pure logic: commands from the bus, what Dev did in the chat and the clock go in;
// messages to send (outbox) and events for the bus come out. No I/O: main.cpp delivers the outbox through the
// Telegram API and reports back with sent() / failed(), so everything here runs in the tests without a network.
//
//   ask      → a message with one button per option, or one that opens the reply field (free text)
//   answer   ← a button tap, a reply to that message, or any plain message while exactly one question is open.
//              With several open, Buddy asks which one the text is for.
//   closed   → the question's message is edited ("✓ tea"), its buttons disappear
//
// Questions survive restarts (dump()/load()); answered and expired ones are kept for a day, so an asker repeating
// its `ask` gets the result again instead of Dev being asked twice. Notices and edits still queued are not kept.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "command.hpp"

namespace buddy::messenger {

// Wall clock: an expiry has to survive a restart
using Time = std::chrono::system_clock::time_point;

struct Button {
    std::string text;
    std::string data;  // comes back in Incoming::data when tapped (≤ 64 bytes)

    bool operator==(const Button&) const = default;
};

struct Send {
    std::uint64_t tag = 0;  // names this message in sent() / failed()
    std::string text;
    std::vector<Button> buttons;  // one per row
    bool force_reply = false;     // Dev's app opens the reply field
    bool silent = false;
};

// New text for a message; its buttons go away
struct Edit {
    std::int64_t message_id = 0;
    std::string text;
};

// Ends the spinner on a tapped button; text shows as a toast
struct AnswerCallback {
    std::string callback_id;
    std::string text;
};

using Out = std::variant<Send, Edit, AnswerCallback>;

// Something Dev did in the chat. A message: message_id, text, reply_to. A button tap: callback_id, data, and
// message_id = the message the button sits on.
struct Incoming {
    std::int64_t message_id = 0;
    std::int64_t reply_to = 0;  // message_id Dev replied to, 0 = none
    std::string text;
    std::string callback_id;
    std::string data;
};

struct Event {
    std::string event;  // asked | answered | expired | failed
    std::string id;
    std::string text;  // answered: what Dev said
    int option = -1;   // answered: index into the options, -1 = Dev typed something else
    std::string reason;

    nlohmann::json to_json() const;
    bool operator==(const Event&) const = default;
};

struct ChatConfig {
    std::chrono::seconds default_ttl{std::chrono::hours(24)};  // for an ask without ttl_s; 0 = never
    std::chrono::seconds keep_closed{std::chrono::hours(24)};
    std::size_t max_open = 20;    // a looping asker must not flood Dev's chat
    std::size_t max_queued = 50;  // notices waiting while Telegram is away; more are dropped
};

// "What's the status?" typed as plain text: Dev asking for /status without the slash. ASCII case-insensitive,
// punctuation ignored.
bool is_status_question(std::string_view text);

class Chat {
public:
    // status: text for the /status command (the services), from whoever knows
    explicit Chat(ChatConfig cfg = {}, std::function<std::string()> status = {});

    // dump() of an earlier run. Questions that never reached the chat are queued again; bad entries are skipped.
    void load(const nlohmann::json& saved, Time now);
    nlohmann::json dump() const;
    // True once after a change worth writing (ask, answer, expiry): the caller saves dump(). Rare by design (microSD).
    bool take_dirty();

    // False: a notice was dropped because max_queued are waiting.
    bool command(const Command& command, Time now);
    void incoming(const Incoming& in, Time now);
    // Expires questions; call when deadline() is reached.
    void tick(Time now);
    std::optional<Time> deadline() const;

    // To deliver in order, front first. The caller pops what it delivered or gave up on, then reports a Send.
    std::deque<Out>& outbox() { return outbox_; }
    void sent(std::uint64_t tag, std::int64_t message_id);
    // The message can't be sent, now or later (not: Telegram unreachable). A question fails and is forgotten, so
    // the asker may try again.
    void failed(std::uint64_t tag, const std::string& reason);

    std::vector<Event> take_events();
    std::size_t open() const;  // questions waiting for Dev

private:
    struct Question {
        std::string id;
        int n = 0;  // short number for button data; never reused
        std::string text;
        std::vector<std::string> options;
        std::string from;
        std::int64_t expires_at = 0;  // unix s, 0 = never
        std::int64_t message_id = 0;  // 0 = not in the chat yet
        std::string result;           // "" = open, else answered | expired
        std::string answer;
        int option = -1;
        std::int64_t closed_at = 0;

        bool is_open() const { return result.empty(); }
    };
    // What a queued Send is for
    struct Tag {
        int question = 0;  // n
        std::string key;   // notice key
    };
    struct Pending {  // text waiting for Dev to pick its question
        int pick = 0;
        std::string text;
    };

    Question* find(const std::string& id);
    Question* by_n(int n);
    Question* by_message(std::int64_t message_id);
    std::vector<Question*> answerable();
    Event event_of(const Question& q) const;

    Send* queued(std::uint64_t tag);
    void unqueue(std::uint64_t tag);
    void queue_question(const Question& q);
    void say(std::string text);
    bool notify(const Notify& n);
    void ask(const Ask& a, Time now);
    void cancel(const Cancel& c);
    void answer(Question& q, const std::string& text, int option, Time now);
    void close(Question& q, const std::string& result, const std::string& line, Time now);
    void on_button(const Incoming& in, Time now);
    void on_bot_command(const std::string& text);

    ChatConfig cfg_;
    std::function<std::string()> status_;
    std::vector<Question> questions_;
    int next_n_ = 1;
    std::int64_t last_message_id_ = 0;  // Dev's newest handled message: Telegram resends after a crash
    std::deque<Out> outbox_;
    std::uint64_t next_tag_ = 1;
    std::map<std::uint64_t, Tag> tags_;
    std::map<std::string, std::int64_t> keys_;  // notice key → its message
    std::optional<Pending> pending_;
    int next_pick_ = 1;
    std::vector<Event> events_;
    bool dirty_ = false;
};

} // namespace buddy::messenger
