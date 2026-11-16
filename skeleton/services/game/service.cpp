#include "service.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "game_host.hpp"
#include "game_store.hpp"
#include "riftbound/ai/ismcts.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/replay.hpp"

namespace buddy::game {

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

struct ServiceConfig {
    riftbound::ai::IsmctsOptions ismcts = [] {
        riftbound::ai::IsmctsOptions o;
        o.budget_ms = env_int("BUDDY_GAME_BUDGET_MS", 2000);  // #38: 2 s on the Pi, p95 2.003 s
        o.threads = env_int("BUDDY_GAME_THREADS", 3);         // the 4th core stays for ears
        return o;
    }();
    int table_timeout_s = env_int("BUDDY_GAME_TABLE_TIMEOUT_S", 30);
    bool voice = env_int("BUDDY_GAME_VOICE", 1) != 0;
    fs::path games = env_string("BUDDY_GAME_GAMES", env_string("HOME", ".") + "/buddy/data/riftbound/games");
};

// Everything the main loop reacts to, in arrival order.
struct Input {
    enum class Kind : std::uint8_t { Command, TableEvent, TableStatus, AiAnswer } kind = Kind::Command;
    std::string payload;
    std::uint64_t game = 0, id = 0;  // AiAnswer: the game it was asked in, the question
    riftbound::Action action;
};

// The AI's thread: one search at a time, the answer back on the main queue. `game` tells answers of an old game
// apart from the new one's (question ids start over every game).
class AiWorker {
public:
    AiWorker(const riftbound::ai::IsmctsOptions& o, BlockingQueue<Input>& answers)
        : options_(o), answers_(answers), thread_([this] { loop(); }) {}
    ~AiWorker() {
        jobs_.close();
        thread_.join();
    }

    void begin(std::uint64_t game, const SessionConfig& cfg) { jobs_.push({game, cfg, std::nullopt}); }
    void ask(std::uint64_t game, AiRequest r) { jobs_.push({game, std::nullopt, std::move(r)}); }

private:
    struct Job {
        std::uint64_t game = 0;
        std::optional<SessionConfig> begin;
        std::optional<AiRequest> ask;
    };

    void loop() {
        std::unique_ptr<riftbound::ai::IsmctsPlayer> player;
        std::uint64_t game = 0;
        while (auto job = jobs_.pop(std::chrono::hours(24 * 365))) {
            if (job->begin) {
                player = std::make_unique<riftbound::ai::IsmctsPlayer>(
                    (std::uint64_t{std::random_device{}()} << 32) | std::random_device{}(), options_);
                player->begin(job->begin->buddy, job->begin->decks[0], job->begin->decks[1]);
                game = job->game;
                continue;
            }
            if (!player || job->game != game) continue;  // asked in a game that's gone
            Input in;
            in.kind = Input::Kind::AiAnswer;
            in.game = job->game;
            in.id = job->ask->id;
            in.action = player->choose(job->ask->view, job->ask->actions);
            answers_.push(std::move(in));
        }
    }

    riftbound::ai::IsmctsOptions options_;
    BlockingQueue<Input>& answers_;
    BlockingQueue<Job> jobs_;
    std::thread thread_;
};

// "vi-unleashed.txt" → "vi": what `new {buddy: …}` names.
HostConfig host_config(const riftbound::CardDb& db, const fs::path& dir) {
    HostConfig cfg;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".txt") continue;
        std::ifstream in(e.path());
        std::stringstream text;
        text << in.rdbuf();
        const std::string stem = e.path().stem().string();
        cfg.decks[stem.substr(0, stem.find('-'))] = {riftbound::parse_deck(db, text.str()), text.str()};
    }
    if (cfg.decks.size() < 2) throw std::runtime_error("need two decks in " + dir.string());
    return cfg;
}

std::string status_line(const GameHost& host) {
    const Session* s = host.session();
    if (s == nullptr) return "idle";
    std::string line = std::string(to_string(s->status()));
    if (s->status() == Status::Paused) line += " (" + std::string(to_string(s->pause())) + ")";
    return "turn " + std::to_string(s->state().turn) + ", " + line;
}

} // namespace

int run_service(const riftbound::CardDb& db, const fs::path& decks_dir) {
    const ServiceConfig cfg;
    if (cfg.ismcts.budget_ms < 100) throw std::invalid_argument("BUDDY_GAME_BUDGET_MS must be ≥ 100");
    if (cfg.ismcts.threads < 1) throw std::invalid_argument("BUDDY_GAME_THREADS must be ≥ 1");
    if (cfg.table_timeout_s < 1) throw std::invalid_argument("BUDDY_GAME_TABLE_TIMEOUT_S must be ≥ 1");
    const HostConfig host_cfg = host_config(db, decks_dir);
    GameStore store(cfg.games);

    BlockingQueue<Input> queue;
    on_stop([&] { queue.close(); });
    AiWorker ai(cfg.ismcts, queue);
    std::uint64_t game = 0;  // the AI's game: answers of another one are dropped

    std::unique_ptr<GameHost> host;
    HostOutputs open;
    {
        Resumed r = resume_latest(store, db, host_cfg);
        if (!r.note.empty()) std::cout << "[game] " << r.note << std::endl;
        if (r.host) {
            host = std::move(r.host);
            open = std::move(r.open);
        } else {
            host = std::make_unique<GameHost>(db, host_cfg);
            open.state = host->state();
        }
    }
    std::cout << "[game] decks";
    for (const auto& [key, _] : host_cfg.decks) std::cout << " " << key;
    std::cout << ", AI " << cfg.ismcts.budget_ms << " ms on " << cfg.ismcts.threads << " threads, table timeout "
              << cfg.table_timeout_s << " s, games in " << cfg.games.string() << std::endl;

    Mqtt mqtt("game", mqtt_config_from_env());
    const auto push = [&](Input::Kind k) {
        return [&queue, k](const std::string&, const std::string& payload) {
            Input in;
            in.kind = k;
            in.payload = payload;
            queue.push(std::move(in));
        };
    };
    mqtt.subscribe(topic::game_cmd, push(Input::Kind::Command));
    mqtt.subscribe(topic::table_event, push(Input::Kind::TableEvent));
    mqtt.subscribe(topic::status("table"), push(Input::Kind::TableStatus));
    if (!mqtt.start()) return 0;

    // The order per input: the files, then everything that leaves (a restart resends what's open).
    const auto send = [&](HostOutputs o) {
        if (o.new_game) {
            const Session& s = *host->session();
            store.begin(file_stamp(), s.config().seed);
            ai.begin(++game, s.config());
            std::cout << "[game] new game, seed " << s.config().seed << ", " << store.journal_path().filename().string()
                      << std::endl;
        }
        store.append(o.journal, o.replay);
        for (const auto& c : o.table) mqtt.publish(topic::table_cmd, table::to_json(c).dump(), 1);
        if (o.ai) ai.ask(game, std::move(*o.ai));
        if (cfg.voice) {
            for (const auto& line : o.voice) mqtt.publish(topic::voice_say, json{{"text", line}}.dump(), 1);
        }
        if (o.state) mqtt.publish(topic::game_state, o.state->dump(), 1, true);
    };

    if (host->session()) ai.begin(++game, host->session()->config());  // a resumed game
    send(std::move(open));

    std::int64_t timed_seq = 0;  // the command the clock runs for
    Clock::time_point deadline{};
    bool table_was_offline = false;
    Status last_status = Status::Setup;
    Pause last_pause = Pause::None;
    std::string last_line;

    while (running()) {
        systemd::notify_watchdog();  // also while the AI thinks: it has its own thread
        auto wait = std::chrono::milliseconds(5000);
        if (timed_seq != 0) {
            wait = std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()),
                              std::chrono::milliseconds(0), wait);
        }
        const auto in = queue.pop(wait);
        if (in) {
            try {
                switch (in->kind) {
                    case Input::Kind::Command: {
                        const json cmd = json::parse(in->payload, nullptr, /*allow_exceptions=*/false);
                        const Reply r = host->command(cmd);
                        if (!r.ok) std::cout << "[game] refused " << in->payload << ": " << r.message << std::endl;
                        break;
                    }
                    case Input::Kind::TableEvent:
                        host->on_table(table::parse_event(json::parse(in->payload)));
                        break;
                    case Input::Kind::TableStatus:
                        // offline → online is only a lost broker link (frozen, broker restarted): resend what's in
                        // flight. A table that really restarted lost its piles and answers `no_game` (then or at the
                        // next command) → TableRestart.
                        if (in->payload == "offline") {
                            table_was_offline = true;
                        } else if (in->payload == "online" && table_was_offline) {
                            table_was_offline = false;
                            if (const auto c = host->table_reconnected()) {
                                std::cout << "[game] table back, resending seq " << c->seq << std::endl;
                                mqtt.publish(topic::table_cmd, table::to_json(*c).dump(), 1);
                            }
                        }
                        break;
                    case Input::Kind::AiAnswer:
                        if (in->game == game) host->on_ai(in->id, in->action);
                        break;
                }
            } catch (const std::exception& e) {
                std::cout << "[game] " << e.what() << std::endl;
            }
        } else if (timed_seq != 0 && Clock::now() >= deadline) {
            std::cout << "[game] no answer from the table to seq " << timed_seq << " in " << cfg.table_timeout_s << " s"
                      << std::endl;
            host->table_timeout();
        }
        send(host->take());

        // the clock: per command in flight, stopped while paused (resume resends: a new clock)
        const Session* s = host->session();
        const table::Command* c = s ? s->inflight() : nullptr;
        if (c == nullptr || s->status() == Status::Paused) {
            timed_seq = 0;
        } else if (c->seq != timed_seq) {
            timed_seq = c->seq;
            deadline = Clock::now() + std::chrono::seconds(cfg.table_timeout_s);
        }

        if (s && (s->status() != last_status || s->pause() != last_pause)) {
            if (s->status() == Status::Paused) std::cout << "[game] paused: " << s->pause_message() << std::endl;
            if (s->status() == Status::Over && last_status != Status::Over) {
                const auto& st = s->state();
                std::cout << "[game] over after " << st.turn << " turns: "
                          << (st.winner ? (*st.winner == s->buddy() ? "Buddy wins" : "Dev wins") : "no winner")
                          << std::endl;
            }
            last_status = s->status();
            last_pause = s->pause();
        }
        if (const std::string line = status_line(*host); line != last_line) {
            systemd::notify_status(line);
            last_line = line;
        }
    }
    mqtt.stop();
    return 0;
}

} // namespace buddy::game
