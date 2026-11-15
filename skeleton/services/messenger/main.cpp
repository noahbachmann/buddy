// messenger: Buddy's line to Dev's phone. Notices and questions from any service go to Dev's private Telegram chat
// with Buddy's bot; answers come back on the bus (D22).
//
//   buddy-messenger                    live: buddy/messenger/cmd in, buddy/messenger/event + retained
//                                      buddy/messenger/state out. Also watches buddy/+/status (watch.hpp).
//   buddy-messenger --pair             once: asks for the bot token from @BotFather, waits for Dev to send the bot a
//                                      code, saves the key file. Then restart the unit.
//   buddy-messenger --notify "text"    test: sends one notice (no MQTT; fine while the unit runs)
//   buddy-messenger --ask "text" [option…]
//                                      test: asks, waits for Dev, prints the answer (no MQTT). Stop the unit first:
//                                      Telegram hands Dev's messages to one listener only.
//
// Only outbound HTTPS to api.telegram.org. Message texts never go to the journal: ids, counts and Telegram's own
// error texts only.
//
// Config (env, see deploy/config/buddy.env.example): BUDDY_MESSENGER_*
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <variant>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <termios.h>
#include <unistd.h>

#include "buddy/config.hpp"
#include "buddy/curl_http.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "chat.hpp"
#include "command.hpp"
#include "store.hpp"
#include "telegram.hpp"
#include "watch.hpp"

namespace {

using namespace buddy;
using namespace buddy::messenger;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr int kPollS = 50;                      // getUpdates waits this long for something new
constexpr seconds kSendGap{1};                  // Telegram: at most one message per second into a chat
constexpr std::size_t kMaxBody = 16u << 20;     // a full batch of 100 long messages stays far below
const char* kNotPaired = "not paired";

struct Config {
    std::string key_file = env_string("BUDDY_MESSENGER_KEY_FILE", env_string("HOME", ".") + "/buddy/config/messenger.json");
    std::string data_dir = env_string("BUDDY_MESSENGER_DATA_DIR", env_string("HOME", ".") + "/buddy/data/messenger");
    int ttl_s = env_int("BUDDY_MESSENGER_TTL_S", 24 * 3600);
    int offline_s = env_int("BUDDY_MESSENGER_OFFLINE_S", 300);
    std::string watch = env_string("BUDDY_MESSENGER_WATCH", "ears brain spotify hue web voice motion speakers table");

    Config() {
        if (ttl_s < 0) throw std::invalid_argument("BUDDY_MESSENGER_TTL_S must not be negative");
        if (offline_s < 0) throw std::invalid_argument("BUDDY_MESSENGER_OFFLINE_S must not be negative");
    }
};

std::vector<std::string> words(const std::string& list) {
    std::string spaced = list;
    std::ranges::replace(spaced, ',', ' ');
    std::istringstream in(spaced);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// One long poll: what arrived within timeout_s, at once when something is waiting. Empty on stop.
// Throws ApiError, or HttpError with the token redacted.
std::vector<Update> fetch_updates(CurlHttp& http, const Telegram& telegram, std::int64_t offset, int timeout_s) {
    std::string body;
    const auto t0 = Clock::now();
    long status = 0;
    try {
        status = http.stream(
            telegram.updates_url(offset, timeout_s), {},
            [&](std::string_view chunk) {
                if (body.size() + chunk.size() <= kMaxBody) body += chunk;
            },
            // stream() has no timeout of its own: give up on a poll that outlives Telegram's
            [&] { return running() && Clock::now() - t0 < seconds(timeout_s + 20); });
    } catch (const HttpError& e) {
        throw HttpError(telegram.redact(e.what()));
    }
    if (!running()) return {};
    return parse_updates(status, body);  // a poll cut short isn't JSON: ApiError, retried
}

// --- live ------------------------------------------------------------------------------------------------------------

struct Cmd {
    Command command;
};
// What Dev did, from one getUpdates batch. The poll thread confirms the batch to Telegram only after main handled
// and saved it (Ack): a crash in between gets it again.
struct Polled {
    std::vector<Incoming> incoming;
};
struct Link {  // the poll thread's view of Telegram
    std::string state;
    std::string detail;
};
struct Status {
    std::string service;
    bool online;
};
using Item = std::variant<Cmd, Polled, Link, Status>;

struct Ack {
    void done() {
        {
            std::lock_guard lock(mutex);
            ++handled;
        }
        cv.notify_all();
    }
    void wait(std::uint64_t batch) {
        std::unique_lock lock(mutex);
        while (running() && handled < batch) cv.wait_for(lock, milliseconds(200));
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t handled = 0;
};

// Long-polls getUpdates on its own thread and connection until running() is false. Backoff 1 → 60 s.
void poll(const Credentials& creds, BlockingQueue<Item>& queue, Ack& ack) {
    CurlHttp http;
    const Telegram telegram(creds.token, http);
    std::int64_t offset = 0;  // not kept across restarts: Telegram resends what wasn't confirmed, Chat drops repeats
    std::uint64_t batches = 0;
    auto backoff = seconds(1);
    int timeout_s = 0;  // first one returns at once: the link state shouldn't wait for a quiet chat's long poll
    std::string link;
    auto report = [&](const std::string& state, const std::string& detail = "") {
        if (state == link) return;
        link = state;
        queue.push(Link{state, detail});
    };
    while (running()) {
        auto wait = backoff;
        try {
            const auto updates = fetch_updates(http, telegram, offset, timeout_s);
            if (!running()) break;
            timeout_s = kPollS;
            Polled batch;
            for (const auto& u : updates) {
                offset = u.update_id + 1;
                if (u.from(creds)) batch.incoming.push_back(u.in);  // anyone else: no answer, no trace
            }
            report("ok");
            backoff = seconds(1);
            if (!batch.incoming.empty()) {
                queue.push(std::move(batch));
                ack.wait(++batches);
            }
            continue;
        } catch (const ApiError& e) {
            switch (classify(e)) {
                case Failure::Unauthorized:
                    report("unauthorized", e.what());
                    wait = seconds(60);
                    break;
                case Failure::Conflict:  // --ask running, or a webhook set on the bot
                    report("conflict", e.what());
                    wait = seconds(10);
                    break;
                default:
                    report("unreachable", e.what());
                    wait = std::max(backoff, seconds(e.retry_after));
            }
        } catch (const HttpError& e) {
            report("unreachable", e.what());
        }
        backoff = std::min(backoff * 2, seconds(60));
        sleep(wait);
    }
}

int serve(const Config& cfg, const std::optional<Credentials>& creds) {
    BlockingQueue<Item> queue;  // HTTP runs on this thread, not on Paho's
    on_stop([&] { queue.close(); });
    Mqtt mqtt("messenger", mqtt_config_from_env());
    mqtt.subscribe(topic::messenger_cmd, [&](const std::string&, const std::string& payload) {
        try {
            queue.push(Cmd{parse_command(payload)});
        } catch (const std::invalid_argument& e) {
            std::cout << "[messenger] bad command (" << payload.size() << " bytes): " << e.what() << std::endl;
        }
    });
    mqtt.subscribe(topic::status("+"), [&](const std::string& topic, const std::string& payload) {
        const auto first = topic.find('/'), last = topic.rfind('/');
        if (first == last) return;
        queue.push(Status{topic.substr(first + 1, last - first - 1), payload == "online"});
    });
    if (!creds)
        std::cout << "[messenger] not paired yet: create a bot with @BotFather, then run ~/buddy/bin/buddy-messenger --pair"
                  << std::endl;

    Watch watch(words(cfg.watch), seconds(cfg.offline_s), Clock::now());
    Chat chat({.default_ttl = seconds(cfg.ttl_s)}, [&] { return watch.summary(); });
    const std::string questions_file = cfg.data_dir + "/questions.json";
    try {
        chat.load(load_json(questions_file), std::chrono::system_clock::now());
    } catch (const std::exception& e) {  // start empty, keep the evidence
        std::cout << "[messenger] " << e.what() << ": moved aside, starting without open questions" << std::endl;
        std::error_code ec;
        std::filesystem::rename(questions_file, questions_file + ".bad", ec);
    }
    if (!mqtt.start()) return 0;

    std::optional<CurlHttp> http;
    std::optional<Telegram> telegram;
    std::thread poller;
    Ack ack;
    if (creds) {
        http.emplace(CurlOptions{.ca_pem = "", .resolve = {}, .timeout_s = 5});  // a stop waits for a running request
        telegram.emplace(creds->token, *http);
        poller = std::thread(poll, std::cref(*creds), std::ref(queue), std::ref(ack));
    }

    std::string poll_link = creds ? "connecting" : kNotPaired;
    std::string send_link;  // trouble sending wins over the poll thread's view; "" = none
    std::string last_state, last_log;
    auto next_send = Clock::now();
    auto send_backoff = seconds(1);

    auto log_once = [&](const std::string& line) {  // each distinct trouble once
        if (line != last_log) std::cout << "[messenger] " << line << std::endl;
        last_log = line;
    };
    // Sends the outbox's first entry; on trouble decides between later and never.
    auto deliver = [&] {
        const Out out = chat.outbox().front();
        const auto* send = std::get_if<Send>(&out);
        const char* what = send ? "message" : std::holds_alternative<Edit>(out) ? "edit" : "button reply";
        try {
            std::int64_t message_id = 0;
            if (send) message_id = telegram->send(creds->chat_id, *send);
            else if (const auto* edit = std::get_if<Edit>(&out)) telegram->edit(creds->chat_id, *edit);
            else telegram->answer_callback(std::get<AnswerCallback>(out));
            chat.outbox().pop_front();
            if (send) {
                chat.sent(send->tag, message_id);
                next_send = Clock::now() + kSendGap;
            }
            if (!send_link.empty()) std::cout << "[messenger] sending works again" << std::endl;
            send_link.clear();
            last_log.clear();
            send_backoff = seconds(1);
            return;
        } catch (const ApiError& e) {
            const Failure failure = classify(e);
            if (failure != Failure::Retry) {  // never: drop it, a question fails
                chat.outbox().pop_front();
                if (send) chat.failed(send->tag, e.what());
                if (failure == Failure::Unauthorized) send_link = "unauthorized";
                if (failure == Failure::Blocked) send_link = "blocked";
                log_once(std::string(what) + " refused: " + e.what());
                return;
            }
            if (e.status != 429) send_link = "unreachable";  // flood control: reached, told to wait
            log_once(std::string("telegram: ") + e.what() + ", retrying");
            next_send = Clock::now() + std::max(send_backoff, seconds(e.retry_after));
        } catch (const HttpError& e) {
            send_link = "unreachable";
            log_once(std::string(e.what()) + ", retrying");
            next_send = Clock::now() + send_backoff;
        }
        send_backoff = std::min(send_backoff * 2, seconds(60));
    };

    while (running()) {
        systemd::notify_watchdog();
        const auto now = Clock::now();
        auto timeout = milliseconds(5000);  // idle: watchdog only; a stop closes the queue
        auto until = [&](Clock::time_point t) {
            timeout = std::clamp(std::chrono::duration_cast<milliseconds>(t - now), milliseconds(0), timeout);
        };
        if (creds && !chat.outbox().empty()) until(next_send);
        if (const auto t = watch.deadline()) until(*t);
        if (const auto t = chat.deadline()) {
            const auto left = *t - std::chrono::system_clock::now();
            timeout = std::clamp(std::chrono::duration_cast<milliseconds>(left), milliseconds(0), timeout);
        }
        auto item = queue.pop(timeout);
        const auto wall = std::chrono::system_clock::now();

        bool batch = false;
        if (item) {
            if (auto* c = std::get_if<Cmd>(&*item)) {
                if (creds) {
                    if (!chat.command(c->command, wall)) log_once("too many notices waiting: dropping new ones");
                } else if (const auto* a = std::get_if<Ask>(&c->command)) {
                    const Event failed{.event = "failed", .id = a->id, .text = "", .option = -1, .reason = kNotPaired};
                    mqtt.publish(topic::messenger_event, failed.to_json().dump(), 1);
                }
            } else if (auto* p = std::get_if<Polled>(&*item)) {
                for (const auto& in : p->incoming) chat.incoming(in, wall);
                batch = true;
            } else if (auto* l = std::get_if<Link>(&*item)) {
                poll_link = l->state;
                std::cout << "[messenger] telegram: " << l->state << (l->detail.empty() ? "" : " (" + l->detail + ")") << std::endl;
            } else if (auto* s = std::get_if<Status>(&*item)) {
                watch.status(s->service, s->online, Clock::now());
            }
        }
        for (const auto& notice : watch.step(Clock::now())) {
            if (creds && !chat.command(notice, wall)) log_once("too many notices waiting: dropping new ones");
        }
        chat.tick(wall);
        if (creds && !chat.outbox().empty() && Clock::now() >= next_send) deliver();
        // nothing left to try (expired, withdrawn): the poll thread's view counts again
        if (chat.outbox().empty() && send_link == "unreachable") send_link.clear();

        // save, then tell: an asker that missed the event asks again and gets it from the file
        if (chat.take_dirty()) {
            try {
                save_json(questions_file, chat.dump());
            } catch (const std::exception& e) {
                log_once(e.what());
            }
        }
        if (batch) ack.done();
        for (const auto& e : chat.take_events()) {
            mqtt.publish(topic::messenger_event, e.to_json().dump(), 1);
            std::cout << "[messenger] " << e.event << " " << e.id << (e.reason.empty() ? "" : ": " + e.reason) << std::endl;
        }
        // counts only: every /state topic is on the login-less panel
        const std::string state = json{{"link", send_link.empty() ? poll_link : send_link}, {"open", chat.open()}}.dump();
        if (state != last_state) {
            mqtt.publish(topic::messenger_state, state, 1, true);
            last_state = state;
        }
    }

    if (poller.joinable()) poller.join();  // stream() polls running() about once a second
    mqtt.stop();
    return 0;
}

// --- command line ----------------------------------------------------------------------------------------------------

// A line from the terminal without showing it (scrollback, shoulder); plain read when piped.
std::string read_secret(const std::string& prompt) {
    std::cout << prompt << std::flush;
    termios old{};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios hidden = old;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
    }
    std::string line;
    std::getline(std::cin, line);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::cout << std::endl;
    }
    return trim(line);
}

const char* kStopUnit = "someone else is listening to this bot. Stop the unit first: systemctl --user stop buddy@messenger";

int pair(const Config& cfg) {
    const std::string token = read_secret("Bot token from @BotFather (not shown): ");
    if (token.empty()) throw std::runtime_error("no token");
    CurlHttp http;
    Telegram telegram(token, http);
    Telegram::Me me;
    try {
        me = telegram.get_me();
    } catch (const ApiError& e) {
        if (classify(e) == Failure::Unauthorized) throw std::runtime_error("Telegram doesn't know this token: copy it again from @BotFather");
        throw;
    }

    // what waited before the code existed can't carry it (and mustn't count as a wrong guess)
    std::int64_t offset = 0;
    auto fetch = [&](int timeout_s) {
        try {
            const auto updates = fetch_updates(http, telegram, offset, timeout_s);
            if (!updates.empty()) offset = updates.back().update_id + 1;
            return updates;
        } catch (const ApiError& e) {
            if (classify(e) == Failure::Conflict) throw std::runtime_error(kStopUnit);
            throw;
        }
    };
    fetch(0);

    std::random_device random;
    const std::string code = std::to_string(std::uniform_int_distribution<int>(100000, 999999)(random));
    std::cout << "Bot @" << me.username << " found. On your phone, open\n\n    https://t.me/" << me.username << "?start=" << code
              << "\n\nand tap Start (or send the bot the code " << code << "). Waiting 5 minutes…" << std::endl;

    const auto deadline = Clock::now() + std::chrono::minutes(5);
    int strangers = 0;
    std::optional<Credentials> creds;
    while (!creds && running() && Clock::now() < deadline) {
        for (const auto& u : fetch(25)) {
            if (u.carries_code(code)) creds = Credentials{.token = token, .user_id = u.user_id, .chat_id = u.chat_id};
            // the bot's name is public: don't let anyone guess codes for five minutes
            else if (!u.in.text.empty() && ++strangers >= 5) throw std::runtime_error("too many wrong codes: run --pair again");
        }
    }
    if (!running()) return 1;
    if (!creds) throw std::runtime_error("no code within 5 minutes");
    telegram.confirm(offset);  // the unit mustn't see the code message as a new one

    save_credentials(cfg.key_file, *creds);
    telegram.set_commands();
    telegram.send(creds->chat_id, {.tag = 0,
                                   .text = "Buddy here ✓ I'll write when something needs you, and ask when I need an answer.",
                                   .buttons = {},
                                   .force_reply = false,
                                   .silent = false});
    std::cout << "✓ paired, saved to " << cfg.key_file << "\n  Now: systemctl --user restart buddy@messenger" << std::endl;
    return 0;
}

int notify(const Credentials& creds, const std::string& text) {
    CurlHttp http;
    Telegram telegram(creds.token, http);
    telegram.send(creds.chat_id, {.tag = 0, .text = clip(trim(text), kMaxText), .buttons = {}, .force_reply = false, .silent = false});
    std::cout << "✓ sent" << std::endl;
    return 0;
}

// The live path without MQTT: the same Chat, delivered and polled inline.
int ask(const Credentials& creds, const std::string& text, std::vector<std::string> options) {
    CurlHttp http;
    Telegram telegram(creds.token, http);
    for (auto& o : options) o = clip(trim(o), kMaxOption);
    Chat chat;
    chat.command(Ask{.id = "cli", .text = clip(trim(text), kMaxText), .options = std::move(options), .from = "", .ttl_s = 0},
                 std::chrono::system_clock::now());
    std::int64_t offset = 0;
    std::optional<Event> answer;
    auto drain = [&] {
        while (!chat.outbox().empty()) {
            const Out out = chat.outbox().front();
            chat.outbox().pop_front();
            if (const auto* s = std::get_if<Send>(&out)) chat.sent(s->tag, telegram.send(creds.chat_id, *s));
            else if (const auto* e = std::get_if<Edit>(&out)) telegram.edit(creds.chat_id, *e);
            else telegram.answer_callback(std::get<AnswerCallback>(out));
        }
        for (auto& e : chat.take_events()) {
            if (e.event == "asked") std::cout << "asked, waiting for the answer… (Ctrl+C gives up)" << std::endl;
            else answer = std::move(e);
        }
    };
    // what waited before the question isn't an answer to it
    try {
        for (const auto& u : fetch_updates(http, telegram, 0, 0)) offset = u.update_id + 1;
        drain();
        while (!answer && running()) {
            for (const auto& u : fetch_updates(http, telegram, offset, 25)) {
                offset = u.update_id + 1;
                if (u.from(creds)) chat.incoming(u.in, std::chrono::system_clock::now());
            }
            drain();
        }
        if (!answer) {  // given up: don't leave a question in the chat that nobody waits for
            chat.command(Cancel{"cli"}, std::chrono::system_clock::now());
            drain();
        }
        if (offset != 0) telegram.confirm(offset);  // nothing left over for the unit
    } catch (const ApiError& e) {
        if (classify(e) == Failure::Conflict) throw std::runtime_error(kStopUnit);
        throw;
    }
    if (!answer) return 1;
    std::cout << "answer: \"" << answer->text << "\"" << (answer->option >= 0 ? " (option " + std::to_string(answer->option) + ")" : "")
              << std::endl;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const std::string mode = argc > 1 ? argv[1] : "";
    const bool ok = argc == 1 || (mode == "--pair" && argc == 2) || (mode == "--notify" && argc == 3) ||
                    (mode == "--ask" && argc >= 3 && static_cast<std::size_t>(argc) <= 3 + kMaxOptions);
    if (!ok) {
        std::cerr << "usage: " << argv[0] << " [--pair | --notify <text> | --ask <text> [option…]]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[messenger] " << e.what() << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int rc = 0;
    try {
        if (mode == "--pair") {
            rc = pair(*cfg);
        } else {
            const auto creds = load_credentials(cfg->key_file);
            if (argc == 1) {
                rc = serve(*cfg, creds);
            } else {
                if (!creds) throw std::runtime_error("not paired yet: run --pair first");
                if (mode == "--notify") rc = notify(*creds, argv[2]);
                else rc = ask(*creds, argv[2], std::vector<std::string>(argv + 3, argv + argc));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "✗ " << e.what() << std::endl;
        rc = 1;
    }
    curl_global_cleanup();
    return rc;
}
