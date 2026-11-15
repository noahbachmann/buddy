#include "chat.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace buddy::messenger {

namespace {

using json = nlohmann::json;

constexpr const char* kHelp = "I write when something needs you, and ask when I need an answer.\n/status: how I'm doing\n/open: "
                              "open questions again";
constexpr const char* kClosed = "That question is closed.";
constexpr std::size_t kMaxKeys = 64;
constexpr std::size_t kAnswerShown = 300;  // bytes of the answer in the edited question

std::int64_t unix_s(Time t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

std::string shown(const std::string& from, const std::string& text) {
    return "❓ " + (from.empty() ? "" : from + ": ") + text;
}

// ASCII case only: enough to match "Tea" typed for the button "tea"
bool same_text(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

// "12:3" → {12, 3}
std::optional<std::pair<int, int>> two_numbers(std::string_view s) {
    int a = 0, b = 0;
    const char* end = s.data() + s.size();
    const auto first = std::from_chars(s.data(), end, a);
    if (first.ec != std::errc{} || first.ptr == end || *first.ptr != ':') return std::nullopt;
    const auto second = std::from_chars(first.ptr + 1, end, b);
    if (second.ec != std::errc{} || second.ptr != end) return std::nullopt;
    return std::pair{a, b};
}

} // namespace

json Event::to_json() const {
    json j{{"event", event}, {"id", id}};
    if (event == "answered") {
        j["text"] = text;
        if (option >= 0) j["option"] = option;
    }
    if (!reason.empty()) j["reason"] = reason;
    return j;
}

Chat::Chat(ChatConfig cfg, std::function<std::string()> status) : cfg_(cfg), status_(std::move(status)) {}

// --- lookups ---------------------------------------------------------------------------------------------------------

Chat::Question* Chat::find(const std::string& id) {
    const auto it = std::ranges::find(questions_, id, &Question::id);
    return it == questions_.end() ? nullptr : &*it;
}

Chat::Question* Chat::by_n(int n) {
    const auto it = std::ranges::find(questions_, n, &Question::n);
    return it == questions_.end() ? nullptr : &*it;
}

Chat::Question* Chat::by_message(std::int64_t message_id) {
    if (message_id == 0) return nullptr;
    const auto it = std::ranges::find(questions_, message_id, &Question::message_id);
    return it == questions_.end() ? nullptr : &*it;
}

// Open and visible to Dev
std::vector<Chat::Question*> Chat::answerable() {
    std::vector<Question*> out;
    for (auto& q : questions_)
        if (q.is_open() && q.message_id != 0) out.push_back(&q);
    return out;
}

std::size_t Chat::open() const {
    return static_cast<std::size_t>(std::ranges::count_if(questions_, &Question::is_open));
}

Event Chat::event_of(const Question& q) const {
    if (q.result == "answered") return {.event = "answered", .id = q.id, .text = q.answer, .option = q.option, .reason = ""};
    return {.event = q.is_open() ? "asked" : q.result, .id = q.id, .text = "", .option = -1, .reason = ""};
}

// --- outbox ----------------------------------------------------------------------------------------------------------

Send* Chat::queued(std::uint64_t tag) {
    for (auto& out : outbox_)
        if (auto* s = std::get_if<Send>(&out); s && s->tag == tag) return s;
    return nullptr;
}

void Chat::unqueue(std::uint64_t tag) {
    std::erase_if(outbox_, [&](const Out& out) {
        const auto* s = std::get_if<Send>(&out);
        return s && s->tag == tag;
    });
    tags_.erase(tag);
}

void Chat::queue_question(const Question& q) {
    Send send{.tag = next_tag_++, .text = shown(q.from, q.text), .buttons = {}, .force_reply = q.options.empty(), .silent = false};
    for (std::size_t i = 0; i < q.options.size(); ++i)
        send.buttons.push_back({q.options[i], "q" + std::to_string(q.n) + ":" + std::to_string(i)});
    tags_[send.tag] = {.question = q.n, .key = ""};
    outbox_.push_back(std::move(send));
}

void Chat::say(std::string text) {
    outbox_.push_back(Send{.tag = next_tag_++, .text = std::move(text), .buttons = {}, .force_reply = false, .silent = true});
}

// --- commands --------------------------------------------------------------------------------------------------------

bool Chat::command(const Command& command, Time now) {
    if (const auto* n = std::get_if<Notify>(&command)) return notify(*n);
    if (const auto* a = std::get_if<Ask>(&command)) ask(*a, now);
    else cancel(std::get<Cancel>(command));
    return true;
}

// Without a key: a new message. With one, the notice has a life: an alert is always a new message (it has to
// ring), a later info under the same key rewrites that message ("hue is offline" → "hue is back").
bool Chat::notify(const Notify& n) {
    if (!n.key.empty()) {
        // still waiting to go out: Dev gets only the newest wording
        for (const auto& [tag, t] : tags_) {
            if (t.key != n.key) continue;
            if (Send* s = queued(tag)) {
                s->text = n.text;
                s->silent = s->silent && !n.alert;
                return true;
            }
        }
        if (const auto it = keys_.find(n.key); it != keys_.end() && !n.alert) {
            outbox_.push_back(Edit{.message_id = it->second, .text = n.text});
            return true;
        }
    }
    if (outbox_.size() >= cfg_.max_queued) return false;
    Send send{.tag = next_tag_++, .text = n.text, .buttons = {}, .force_reply = false, .silent = !n.alert};
    if (!n.key.empty()) tags_[send.tag] = {.question = 0, .key = n.key};
    outbox_.push_back(std::move(send));
    return true;
}

void Chat::ask(const Ask& a, Time now) {
    if (const Question* q = find(a.id)) {  // repeated: say where it stands, never ask Dev twice
        if (!q->is_open() || q->message_id != 0) events_.push_back(event_of(*q));
        return;
    }
    if (open() >= cfg_.max_open) {
        events_.push_back({.event = "failed", .id = a.id, .text = "", .option = -1, .reason = "too many open questions"});
        return;
    }
    Question q;
    q.id = a.id;
    q.n = next_n_++;
    q.text = a.text;
    q.options = a.options;
    q.from = a.from;
    const std::int64_t ttl = a.ttl_s ? *a.ttl_s : cfg_.default_ttl.count();
    constexpr std::int64_t kForever = std::int64_t{10} * 365 * 24 * 3600;  // no overflow below
    if (ttl > 0) q.expires_at = unix_s(now) + std::min(ttl, kForever);
    queue_question(q);
    questions_.push_back(std::move(q));
    dirty_ = true;
}

void Chat::cancel(const Cancel& c) {
    Question* q = find(c.id);
    if (!q) return;
    if (q->is_open()) {
        if (q->message_id != 0) outbox_.push_back(Edit{q->message_id, shown(q->from, q->text) + "\n\n✗ withdrawn"});
        for (const auto& [tag, t] : tags_) {
            if (t.question != q->n) continue;
            unqueue(tag);
            break;
        }
    }
    std::erase_if(questions_, [&](const Question& x) { return x.id == c.id; });
    dirty_ = true;
}

// --- what Dev did ----------------------------------------------------------------------------------------------------

void Chat::close(Question& q, const std::string& result, const std::string& line, Time now) {
    q.result = result;
    q.closed_at = unix_s(now);
    if (q.message_id != 0) outbox_.push_back(Edit{q.message_id, shown(q.from, q.text) + "\n\n" + line});
    for (const auto& [tag, t] : tags_) {  // never reached the chat
        if (t.question != q.n) continue;
        unqueue(tag);
        break;
    }
    events_.push_back(event_of(q));
    dirty_ = true;
}

void Chat::answer(Question& q, const std::string& text, int option, Time now) {
    if (option < 0) {
        for (std::size_t i = 0; i < q.options.size(); ++i)
            if (same_text(q.options[i], text)) option = static_cast<int>(i);
    }
    q.answer = option >= 0 ? q.options[static_cast<std::size_t>(option)] : text;
    q.option = option;
    close(q, "answered", "✓ " + clip(q.answer, kAnswerShown), now);
}

void Chat::incoming(const Incoming& in, Time now) {
    if (!in.callback_id.empty()) return on_button(in, now);
    if (in.message_id <= last_message_id_) return;  // handled before a crash, resent by Telegram
    last_message_id_ = in.message_id;

    const std::string text = clip(trim(in.text), kMaxText);
    if (text.empty()) return say("I only read text.");  // photo, sticker, voice message
    if (Question* q = by_message(in.reply_to)) {
        if (q->is_open()) answer(*q, text, -1, now);
        else say(kClosed);
        return;
    }
    if (text.front() == '/') return on_bot_command(text);

    const auto candidates = answerable();
    if (candidates.empty()) return say("Nothing is waiting for an answer.");
    if (candidates.size() == 1) return answer(*candidates.front(), text, -1, now);

    pending_ = Pending{.pick = next_pick_++, .text = text};
    Send which{.tag = next_tag_++, .text = "Which question is that for?", .buttons = {}, .force_reply = false, .silent = true};
    for (const Question* q : candidates)
        which.buttons.push_back({clip(q->text, kMaxOption), "p" + std::to_string(pending_->pick) + ":" + std::to_string(q->n)});
    outbox_.push_back(std::move(which));
}

// Button data: "q<n>:<i>" = option i of question n; "p<pick>:<n>" = the pending text is for question n.
void Chat::on_button(const Incoming& in, Time now) {
    const auto numbers = in.data.size() > 1 ? two_numbers(std::string_view(in.data).substr(1)) : std::nullopt;
    auto toast = [&](const char* text) { outbox_.push_back(AnswerCallback{in.callback_id, text}); };
    if (!numbers) return toast("");

    if (in.data.front() == 'q') {
        Question* q = by_n(numbers->first);
        const int i = numbers->second;
        if (q && q->is_open() && i >= 0 && static_cast<std::size_t>(i) < q->options.size()) {
            toast("");
            return answer(*q, q->options[static_cast<std::size_t>(i)], i, now);
        }
        toast(kClosed);
        // buttons still there: the edit that closed it got lost (restart)
        if (q && !q->is_open() && q->message_id == in.message_id) {
            const std::string line = q->result == "answered" ? "✓ " + clip(q->answer, kAnswerShown) : "⌛ no answer in time";
            outbox_.push_back(Edit{q->message_id, shown(q->from, q->text) + "\n\n" + line});
        }
        return;
    }
    if (in.data.front() == 'p') {
        Question* q = by_n(numbers->second);
        if (pending_ && pending_->pick == numbers->first && q && q->is_open()) {
            toast("");
            outbox_.push_back(Edit{in.message_id, "→ " + clip(q->text, kMaxOption)});
            const std::string text = std::move(pending_->text);
            pending_.reset();
            return answer(*q, text, -1, now);
        }
        toast("Too late for that one, send it again.");
        outbox_.push_back(Edit{in.message_id, "(no longer valid)"});
        return;
    }
    toast("");
}

void Chat::on_bot_command(const std::string& text) {
    std::string name = text.substr(0, text.find_first_of(" @"));  // "/status@my_bot" in Telegram's menus
    if (name == "/status") {
        const std::size_t n = open();
        std::string out = status_ ? status_() : "";
        if (!out.empty()) out += "\n";
        return say(out + (n == 0 ? "No open questions." : std::to_string(n) + (n == 1 ? " open question." : " open questions.")));
    }
    if (name == "/open") {
        const auto candidates = answerable();
        if (candidates.empty()) return say(open() == 0 ? "No open questions." : "On their way.");
        for (Question* q : candidates) {  // down to the end of the chat, with working buttons
            outbox_.push_back(Edit{q->message_id, shown(q->from, q->text) + "\n\n↓ asked again below"});
            q->message_id = 0;
            queue_question(*q);
        }
        dirty_ = true;
        return;
    }
    say(kHelp);
}

// --- delivery reports ------------------------------------------------------------------------------------------------

void Chat::sent(std::uint64_t tag, std::int64_t message_id) {
    const auto it = tags_.find(tag);
    if (it == tags_.end()) return;
    const Tag t = it->second;
    tags_.erase(it);
    if (!t.key.empty()) {
        if (keys_.size() >= kMaxKeys && !keys_.contains(t.key)) keys_.erase(keys_.begin());
        keys_[t.key] = message_id;
        return;
    }
    if (Question* q = by_n(t.question)) {
        q->message_id = message_id;
        events_.push_back(event_of(*q));
        dirty_ = true;
    }
}

void Chat::failed(std::uint64_t tag, const std::string& reason) {
    const auto it = tags_.find(tag);
    if (it == tags_.end()) return;
    const int n = it->second.question;
    tags_.erase(it);
    const Question* q = by_n(n);
    if (!q) return;
    events_.push_back({.event = "failed", .id = q->id, .text = "", .option = -1, .reason = reason});
    std::erase_if(questions_, [&](const Question& x) { return x.n == n; });
    dirty_ = true;
}

std::vector<Event> Chat::take_events() { return std::exchange(events_, {}); }

bool Chat::take_dirty() { return std::exchange(dirty_, false); }

// --- time ------------------------------------------------------------------------------------------------------------

void Chat::tick(Time now) {
    const std::int64_t t = unix_s(now);
    for (auto& q : questions_)
        if (q.is_open() && q.expires_at != 0 && q.expires_at <= t) close(q, "expired", "⌛ no answer in time", now);
    // forgotten without a write of its own: the next save drops them from the file
    std::erase_if(questions_, [&](const Question& q) { return !q.is_open() && q.closed_at + cfg_.keep_closed.count() <= t; });
}

std::optional<Time> Chat::deadline() const {
    std::int64_t first = std::numeric_limits<std::int64_t>::max();
    for (const auto& q : questions_)
        if (q.is_open() && q.expires_at != 0) first = std::min(first, q.expires_at);
    if (first == std::numeric_limits<std::int64_t>::max()) return std::nullopt;
    return Time{} + std::chrono::seconds(first);
}

// --- persistence -----------------------------------------------------------------------------------------------------

json Chat::dump() const {
    json questions = json::array();
    for (const auto& q : questions_) {
        questions.push_back({{"id", q.id},
                             {"n", q.n},
                             {"text", q.text},
                             {"options", q.options},
                             {"from", q.from},
                             {"expires_at", q.expires_at},
                             {"message_id", q.message_id},
                             {"result", q.result},
                             {"answer", q.answer},
                             {"option", q.option},
                             {"closed_at", q.closed_at}});
    }
    return {{"next_n", next_n_}, {"last_message_id", last_message_id_}, {"questions", std::move(questions)}};
}

void Chat::load(const json& saved, Time now) {
    if (!saved.is_object()) return;
    auto number = [&](const char* key) {
        return saved.contains(key) && saved[key].is_number_integer() ? saved[key].get<std::int64_t>() : 0;
    };
    last_message_id_ = number("last_message_id");
    next_n_ = static_cast<int>(std::clamp<std::int64_t>(number("next_n"), 1, std::numeric_limits<int>::max() / 2));
    const json questions = saved.contains("questions") && saved["questions"].is_array() ? saved["questions"] : json::array();
    for (const auto& j : questions) {
        try {
            Question q;
            q.id = j.at("id");
            q.n = j.at("n");
            q.text = j.at("text");
            q.options = j.at("options");
            q.from = j.at("from");
            q.expires_at = j.at("expires_at");
            q.message_id = j.at("message_id");
            q.result = j.at("result");
            q.answer = j.at("answer");
            q.option = j.at("option");
            q.closed_at = j.at("closed_at");
            if (q.id.empty() || find(q.id) || by_n(q.n)) continue;
            if (q.option >= static_cast<int>(q.options.size())) q.option = -1;
            next_n_ = std::max(next_n_, q.n + 1);
            questions_.push_back(std::move(q));
        } catch (const json::exception&) {  // hand-edited file: skip the entry
        }
    }
    for (const auto& q : questions_)
        if (q.is_open() && q.message_id == 0) queue_question(q);
    tick(now);
}

} // namespace buddy::messenger
