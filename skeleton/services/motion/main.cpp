// motion: joints in degrees (MQTT) → protocol v1 on the Pico's USB serial port (guide 2.3, D17).
//
//   buddy-motion                       live: buddy/motion/cmd in, retained buddy/motion/state out; gestures also
//                                      publish their reply to buddy/voice/say
//   buddy-motion --cmd move <joint>=<deg>… [--ms N]
//   buddy-motion --cmd center [joint…] | --cmd relax | --cmd gesture <name>
//                                      test: one command, no MQTT (the Pico relaxes 2 s after it)
//   buddy-motion --raw "<id> <line>"   test: one protocol line, prints the reply
//
// The Pico missing is not an error: the service stays up, reports connected:false and retries the port. It still
// exits 1 for config errors (D14). Config (env, see deploy/buddy.env.example): BUDDY_MOTION_*
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <variant>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "command.hpp"
#include "gestures.hpp"
#include "joints.hpp"
#include "serial_link.hpp"

namespace {

using namespace buddy;
using namespace buddy::motion;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

const char* kNotConnected = "My motors aren't connected.";
const char* kUnknownGesture = "I don't know that move.";
constexpr auto ping_interval = milliseconds(500);  // well inside the Pico's 2 s link timeout

struct Config {
    std::string device = env_string("BUDDY_MOTION_DEVICE", "/dev/buddy-pico");
    bool debug = env_int("BUDDY_MOTION_DEBUG", 0) != 0;
};

// Link + what the service knows about the joints. Every serial call goes through here; a LinkError closes the
// link and forgets all positions (an unpowered or reset servo could be anywhere).
class Motion {
public:
    explicit Motion(const Config& cfg) : cfg_(cfg), link_(cfg.debug) {}

    bool connected() const { return link_.is_open(); }
    const Positions& positions() const { return positions_; }

    // One attempt. Logs each distinct error once (microSD).
    bool connect() {
        try {
            const auto fw = link_.open(cfg_.device, joints());
            std::cout << "[motion] Pico connected, firmware " << fw << std::endl;
            last_error_.clear();
            positions_ = unknown_positions();
            return true;
        } catch (const LinkError& e) {
            report(e.what());
            return false;
        }
    }

    void disconnect(const std::string& why) {
        if (!link_.is_open()) return;
        if (!why.empty()) std::cout << "[motion] Pico link lost: " << why << std::endl;
        link_.close();
        positions_ = unknown_positions();
    }

    // false = not connected or the link broke
    bool send(const Request& r) {
        if (!link_.is_open()) return false;
        try {
            const auto reply = link_.request(r);
            if (reply && reply->kind == Kind::err) std::cout << "[motion] Pico refused: " << reply->line << std::endl;
            return true;
        } catch (const LinkError& e) {
            disconnect(e.what());
            return false;
        }
    }

    // Moves and returns the ramp time; nullopt on a bad command (logged) or a lost link.
    std::optional<milliseconds> move(const std::map<std::string, float>& targets, int ms) {
        auto planned = plan_move(targets, positions_, ms);
        if (auto* err = std::get_if<std::string>(&planned)) {
            std::cout << "[motion] move dropped: " << *err << std::endl;
            return std::nullopt;
        }
        const auto& plan = std::get<MovePlan>(planned);
        for (const auto& r : plan.requests)
            if (!send(r)) return std::nullopt;
        for (const auto& [name, deg] : plan.targets) positions_[name] = deg;
        return milliseconds(plan.ms);
    }

    void relax(const std::set<std::string>& names) {  // empty = all
        if (names.empty()) {
            if (send({.op = Op::relax, .channel = all_channels})) positions_ = unknown_positions();
            return;
        }
        for (const auto& name : names)
            if (const Joint* j = find_joint(name); j && send({.op = Op::relax, .channel = j->channel}))
                positions_[name] = std::nullopt;
    }

    void ping() { send({.op = Op::ping}); }

    // Unsolicited lines: an unexpected `* BOOT` means the Pico reset under us; `* RELAXED` that it let go.
    void poll_events() {
        while (link_.is_open()) {
            if (link_.lost()) {
                disconnect("Pico gone");
                return;
            }
            const auto e = link_.events().pop(milliseconds(0));
            if (!e) return;
            if (e->starts_with("* BOOT")) {
                disconnect("Pico reset (" + *e + ")");
            } else if (e->starts_with("* RELAXED")) {
                std::cout << "[motion] Pico relaxed all servos: " << *e << std::endl;
                positions_ = unknown_positions();
            }
        }
    }

    // --raw: the Pico's reply line
    std::optional<std::string> raw(const std::string& line) { return link_.raw(line); }

private:
    void report(const std::string& error) {
        if (error != last_error_) std::cout << "[motion] " << error << std::endl;
        last_error_ = error;
    }

    Config cfg_;
    SerialLink link_;
    Positions positions_ = unknown_positions();
    std::string last_error_;
};

json state_json(const Motion& m, const GesturePlayer& player) {
    json joints = json::object();
    for (const auto& [name, deg] : m.positions())
        joints[name] = deg ? json(std::round(*deg * 10) / 10) : json(nullptr);
    return {{"connected", m.connected()},
            {"joints", joints},
            {"gesture", player.active() ? json(player.gesture()->name) : json(nullptr)}};
}

// Runs one gesture step; false when the link broke.
bool run_step(Motion& m, GesturePlayer& player, const Step& step) {
    const auto now = Clock::now();
    if (step.relax) {
        m.relax(player.gesture()->joint_names());
        player.wait(milliseconds(0), now);
        return m.connected();
    }
    const auto ramp = m.move(step.joints, step.ms);
    if (!ramp) return false;
    player.wait(*ramp, now);
    return true;
}

int serve(const Config& cfg) {
    BlockingQueue<Command> queue;  // serial I/O on this thread, not on Paho's
    Mqtt mqtt("motion", mqtt_config_from_env());
    mqtt.subscribe(topic::motion_cmd, [&](const std::string&, const std::string& payload) {
        auto parsed = parse_command(payload);
        if (auto* err = std::get_if<std::string>(&parsed)) {
            std::cout << "[motion] bad command (" << *err << "): " << payload << std::endl;
            return;
        }
        queue.push(std::move(std::get<Command>(parsed)));
    });
    if (!mqtt.start()) return 0;

    Motion motion(cfg);
    GesturePlayer player;
    auto say = [&](const std::string& text) { mqtt.publish(topic::voice_say, json{{"text", text}}.dump(), 1); };
    json published;
    auto publish_state = [&] {
        const json state = state_json(motion, player);
        if (state == published) return;
        mqtt.publish(topic::motion_state, state.dump(), 1, true);
        published = state;
    };
    publish_state();

    auto backoff = std::chrono::seconds(1);
    auto next_connect = Clock::now();
    auto next_ping = Clock::now();
    bool reported_disconnected = false;  // "dropped: not connected" once per outage

    auto handle = [&](const Command& cmd) {
        const auto unknown = std::ranges::find_if(cmd.joints, [](const auto& j) { return !find_joint(j.first); });
        if (unknown != cmd.joints.end()) {
            std::cout << "[motion] command dropped: unknown joint " << unknown->first << std::endl;
            return;
        }
        const Gesture* gesture = cmd.action == "gesture" ? find_gesture(cmd.query) : nullptr;
        if (cmd.action != "gesture" || gesture) player.cancel();  // a valid command takes over from a gesture
        if (!motion.connected()) {
            if (cmd.action == "gesture") say(kNotConnected);
            if (!reported_disconnected) std::cout << "[motion] Pico not connected: commands dropped" << std::endl;
            reported_disconnected = true;
        } else if (cmd.action == "move") {
            motion.move(cmd.joints, cmd.ms.value_or(default_move_ms));
        } else if (cmd.action == "center") {
            std::map<std::string, float> targets;
            for (const auto& j : joints())
                if (cmd.joints.empty() || cmd.joints.contains(std::string(j.name))) targets[std::string(j.name)] = 0;
            motion.move(targets, cmd.ms.value_or(default_move_ms));
        } else if (cmd.action == "relax") {
            motion.relax({});
        } else if (gesture) {
            std::cout << "[motion] gesture " << gesture->name << std::endl;
            say(gesture->reply);
            player.start(*gesture, Clock::now());
        } else {
            std::cout << "[motion] unknown gesture \"" << cmd.query << "\"" << std::endl;
            say(kUnknownGesture);
        }
    };

    while (running()) {
        systemd::notify_watchdog();
        auto now = Clock::now();
        if (!motion.connected() && now >= next_connect) {
            if (motion.connect()) {
                backoff = std::chrono::seconds(1);
                reported_disconnected = false;
                next_ping = now + ping_interval;
            } else {
                next_connect = now + backoff;
                backoff = std::min(backoff * 2, std::chrono::seconds(10));
            }
        }

        auto timeout = milliseconds(200);
        auto until = [&](Clock::time_point t) {
            timeout = std::clamp(std::chrono::duration_cast<milliseconds>(t - now), milliseconds(0), timeout);
        };
        if (motion.connected()) until(next_ping);
        if (const auto t = player.next_at()) until(*t);

        if (auto cmd = queue.pop(timeout)) handle(*cmd);

        motion.poll_events();
        if (!motion.connected()) player.cancel();
        now = Clock::now();
        while (const Step* step = player.due(now))
            if (!run_step(motion, player, *step)) player.cancel();
        if (motion.connected() && Clock::now() >= next_ping) {
            motion.ping();
            next_ping = Clock::now() + ping_interval;
        }
        publish_state();
    }

    motion.relax({});  // limp right away, not only after the Pico's 2 s link timeout
    motion.disconnect("");
    publish_state();
    mqtt.stop();
    return 0;
}

// --cmd: one command against the port, no MQTT
int run_cmd(const Config& cfg, int argc, char** argv) {
    Command c{.action = argv[2]};
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ms" && i + 1 < argc) {
            c.ms = std::stoi(argv[++i]);
        } else if (const auto eq = arg.find('='); eq != std::string::npos) {
            c.joints[arg.substr(0, eq)] = std::stof(arg.substr(eq + 1));
        } else if (c.action == "gesture") {
            c.query = arg;
        } else {
            c.joints[arg] = 0;  // center <joint>
        }
    }
    Motion motion(cfg);
    if (!motion.connect()) return 1;
    if (c.action == "move") {
        if (!motion.move(c.joints, c.ms.value_or(default_move_ms))) return 1;
    } else if (c.action == "center") {
        std::map<std::string, float> targets = c.joints;
        if (targets.empty())
            for (const auto& j : joints()) targets[std::string(j.name)] = 0;
        if (!motion.move(targets, c.ms.value_or(default_move_ms))) return 1;
    } else if (c.action == "relax") {
        motion.relax({});
    } else if (c.action == "gesture") {
        const Gesture* g = find_gesture(c.query);
        if (!g) {
            std::cerr << "unknown gesture \"" << c.query << "\"" << std::endl;
            return 1;
        }
        std::cout << "says: \"" << g->reply << "\"" << std::endl;
        GesturePlayer player;
        player.start(*g, Clock::now());
        while (player.active() && running()) {
            if (const Step* step = player.due(Clock::now()); step && !run_step(motion, player, *step)) return 1;
            std::this_thread::sleep_for(milliseconds(10));
        }
    } else {
        std::cerr << "unknown action " << c.action << std::endl;
        return 2;
    }
    std::cout << state_json(motion, GesturePlayer{}).dump() << std::endl;
    return motion.connected() ? 0 : 1;
}

int run_raw(const Config& cfg, const std::string& line) {
    Motion motion(cfg);
    if (!motion.connect()) return 1;
    const auto reply = motion.raw(line);
    std::cout << (reply ? *reply : "(no reply)") << std::endl;
    return reply ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const bool cmd = argc >= 3 && std::strcmp(argv[1], "--cmd") == 0;
    const bool raw = argc == 3 && std::strcmp(argv[1], "--raw") == 0;
    if (argc != 1 && !cmd && !raw) {
        std::cerr << "usage: " << argv[0]
                  << " [--cmd move <joint>=<deg>… [--ms N] | --cmd center [joint…] | --cmd relax | --cmd gesture <name>"
                     " | --raw \"<id> <line>\"]"
                  << std::endl;
        return 2;
    }

    try {
        const Config cfg;
        if (cmd) return run_cmd(cfg, argc, argv);
        if (raw) return run_raw(cfg, argv[2]);
        return serve(cfg);
    } catch (const std::exception& e) {  // config typos, --cmd arguments
        std::cerr << "[motion] " << e.what() << std::endl;
        return 1;
    }
}
