// speakers: where Buddy's sound comes out (D20). Music (Soloist → buddy_music) goes to the selected Nest over
// Bluetooth, connected only while needed; Buddy's voice (buddy_voice) follows it there, else the Jabra; the music is
// ducked while Buddy listens or speaks. Logic in router.*, this file runs the tools: pactl, pw-link, bluetoothctl.
//
//   buddy-speakers                          live: buddy/speakers/cmd, ears + voice state in, buddy/speakers/state out
//   buddy-speakers --cmd list               test: the Nests and whether they're connected (no MQTT)
//   buddy-speakers --cmd connect <name>     test: connect one; --cmd disconnect: all
//   buddy-speakers --pair [alias]           pair a new Nest: say "Hey Google, pair Bluetooth" to it first
//
// No polling: `pactl subscribe` wakes the loop on PipeWire changes. Config (env, see deploy/buddy.env.example):
// BUDDY_SPEAKERS_*
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "parse.hpp"
#include "router.hpp"

extern char** environ;

namespace {

using namespace buddy;
using namespace buddy::speakers;
using namespace std::chrono_literals;
using json = nlohmann::json;

struct Config {
    static inline const RouterConfig defaults{};  // defaults live in router.hpp; env only overrides
    // else spotify's default speaker: usually a Nest
    std::string default_speaker = env_string("BUDDY_SPEAKERS_DEFAULT", env_string("BUDDY_SPOTIFY_DEVICE", ""));
    RouterConfig router{
        .idle = std::chrono::seconds(env_int("BUDDY_SPEAKERS_IDLE_S", static_cast<int>(defaults.idle.count()))),
        .duck_percent = env_int("BUDDY_SPEAKERS_DUCK_PERCENT", defaults.duck_percent),
        .duck_hold = std::chrono::milliseconds(
            env_int("BUDDY_SPEAKERS_DUCK_HOLD_MS", static_cast<int>(defaults.duck_hold.count()))),
        .volume = env_int("BUDDY_SPEAKERS_VOLUME", defaults.volume),
        .voice_percent = env_int("BUDDY_SPEAKERS_VOICE_PERCENT", defaults.voice_percent),
    };
};

struct Output {
    int status = -1;  // exit code; -1 = couldn't start or killed after the timeout
    std::string out;  // stdout (stderr goes to the journal)
};

// Runs a tool, collects its stdout, kills it after `timeout`.
Output run(const std::vector<std::string>& args, std::chrono::milliseconds timeout = 5s) {
    Output result;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return result;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    const int err = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (err != 0) {
        std::cout << "[speakers] cannot run " << args[0] << ": " << std::strerror(err) << std::endl;
        close(fds[0]);
        return result;
    }
    const auto end = std::chrono::steady_clock::now() + timeout;
    char buf[4096];
    bool killed = false;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
        pollfd p{fds[0], POLLIN, 0};
        if (left.count() <= 0 || poll(&p, 1, static_cast<int>(left.count())) == 0) {
            kill(pid, SIGTERM);
            killed = true;
            break;
        }
        const ssize_t n = read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        result.out.append(buf, static_cast<size_t>(n));
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!killed && WIFEXITED(status)) result.status = WEXITSTATUS(status);
    return result;
}

std::vector<Speaker> load_speakers() {
    std::vector<Speaker> out;
    // Trusted, not Paired: the Nests don't bond, so a disconnected one shows "Paired: no" (#16)
    for (const auto& d : parse_devices(run({"bluetoothctl", "devices", "Trusted"}).out)) {
        const auto info = parse_info(run({"bluetoothctl", "info", d.mac}).out);
        if (info.audio_sink) out.push_back({speaker_id(d.mac), info.alias.empty() ? d.name : info.alias, d.mac});
    }
    return out;
}

struct Snapshot {
    World world;
    int voice_stream = -1;  // index of buddy_voice.output
};

Snapshot read_pipewire(const std::vector<Speaker>& speakers) {
    Snapshot snap;
    const auto sinks = parse_sinks(run({"pactl", "-f", "json", "list", "sinks"}).out);
    const auto streams = parse_streams(run({"pactl", "-f", "json", "list", "sink-inputs"}).out);
    int music = -1;
    for (const auto& s : sinks) {
        if (s.name == "buddy_music") music = s.index;
        const bool known = std::ranges::any_of(speakers, [&](const Speaker& sp) { return sp.mac == s.mac; });
        if (!s.mac.empty() && known) snap.world.connected.push_back({s.mac, s.name});
    }
    for (const auto& st : streams) {
        if (st.sink == music && music >= 0 && !st.corked) snap.world.music = true;
        if (st.node_name == "buddy_voice.output") snap.voice_stream = st.index;
    }
    return snap;
}

bool connect(const std::string& mac) {
    // Right after a PipeWire restart the first try fails (no A2DP endpoint yet): one retry
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) std::this_thread::sleep_for(2s);
        systemd::notify_watchdog();
        if (run({"bluetoothctl", "connect", mac}, 15s).out.find("Connection successful") != std::string::npos)
            return true;
    }
    return false;
}

constexpr const char* kA2dpSink = "0000110b-0000-1000-8000-00805f9b34fb";

// Runs the router's actions. Returns the MACs whose connect failed.
std::vector<std::string> execute(const std::vector<Action>& actions, const Snapshot& snap, const Router& router) {
    std::vector<std::string> failed, released;
    for (const auto& a : actions) {
        const Speaker* sp = router.find(a.target);
        const std::string name = sp ? sp->name : a.target;
        switch (a.kind) {
        case Action::Kind::Connect:
            std::cout << "[speakers] connecting " << name << std::endl;
            if (!connect(a.target)) {
                std::cout << "[speakers] " << name << " didn't connect" << std::endl;
                failed.push_back(a.target);
            }
            break;
        case Action::Kind::Disconnect:
            // Only the audio profile (~15 ms): the next Nest can connect once it's free, while a full disconnect
            // takes 2.5–3 s (measured 2026-10-30). That follows in the background, after a connect in this step.
            std::cout << "[speakers] releasing " << name << std::endl;
            run({"bluetoothctl", "disconnect", a.target, kA2dpSink});
            released.push_back(a.target);
            break;
        case Action::Kind::LinkMusic:
            std::cout << "[speakers] music → " << a.target << std::endl;
            // "File exists" from an earlier run is fine
            run({"pw-link", "buddy_music.output:output_FL", a.target + ":playback_FL"});
            run({"pw-link", "buddy_music.output:output_FR", a.target + ":playback_FR"});
            break;
        case Action::Kind::MoveVoice:
            if (snap.voice_stream < 0) break;  // no buddy_voice: PipeWire config missing, voice uses the default
            std::cout << "[speakers] voice → " << (a.target.empty() ? "default sink" : a.target) << std::endl;
            run({"pactl", "move-sink-input", std::to_string(snap.voice_stream),
                 a.target.empty() ? "@DEFAULT_SINK@" : a.target});
            break;
        case Action::Kind::SetVolume:
            run({"pactl", "set-sink-volume", a.target, std::to_string(a.percent) + "%"});
            break;
        }
    }
    for (const auto& mac : released) std::thread([mac] { run({"bluetoothctl", "disconnect", mac}, 10s); }).detach();
    return failed;
}

// `pactl subscribe` in a thread: each PipeWire sink/stream change wakes the main loop. Restarted when PipeWire is.
class PulseWatch {
public:
    explicit PulseWatch(std::function<void()> wake) : wake_(std::move(wake)), thread_([this] { loop(); }) {}
    ~PulseWatch() {
        stop_ = true;
        if (const pid_t p = pid_.load(); p > 0) kill(p, SIGTERM);
        thread_.join();
    }

private:
    void loop() {
        while (!stop_) {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) != 0) return;
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
            char* argv[] = {const_cast<char*>("pactl"), const_cast<char*>("subscribe"), nullptr};
            pid_t pid;
            const int err = posix_spawnp(&pid, "pactl", &actions, nullptr, argv, environ);
            posix_spawn_file_actions_destroy(&actions);
            close(fds[1]);
            if (err == 0) {
                pid_ = pid;
                if (stop_) kill(pid, SIGTERM);  // stopped between spawn and pid_
                wake_();                        // (re)started: state may have changed meanwhile
                std::string buf;
                char chunk[1024];
                ssize_t n;
                while ((n = read(fds[0], chunk, sizeof chunk)) > 0) {
                    buf.append(chunk, static_cast<size_t>(n));
                    bool wake = false;
                    for (size_t nl; (nl = buf.find('\n')) != std::string::npos; buf.erase(0, nl + 1)) {
                        // "Event 'change' on sink-input #12"; client/module events don't matter
                        const std::string_view line(buf.data(), nl);
                        wake = wake || line.find(" on sink") != line.npos || line.find(" on server") != line.npos;
                    }
                    if (wake) wake_();
                }
                waitpid(pid, nullptr, 0);
                pid_ = 0;
            }
            close(fds[0]);
            for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(100ms);
        }
    }

    std::function<void()> wake_;
    std::atomic<bool> stop_{false};
    std::atomic<pid_t> pid_{0};
    std::thread thread_;
};

struct Event {
    enum class Kind { Pulse, Listening, Speaking, Command } kind;
    bool on = false;
    json cmd = nullptr;
};

int offline(const std::vector<std::string>& args, const Config& cfg) {
    Router router(load_speakers(), cfg.router, cfg.default_speaker);
    const auto snap = read_pipewire(router.speakers());
    auto connected = [&](const Speaker& s) {
        return std::ranges::any_of(snap.world.connected, [&](const World::Nest& n) { return n.mac == s.mac; });
    };
    const std::string& action = args[0];
    if (action == "list") {
        if (router.speakers().empty()) std::cout << "no speakers: pair one with --pair" << std::endl;
        for (const auto& s : router.speakers())
            std::cout << "\"" << s.name << "\" " << s.id << (connected(s) ? " (connected)" : "")
                      << (router.selected() == &s ? " (default)" : "") << std::endl;
        return 0;
    }
    if (action == "connect" && args.size() == 2) {
        if (auto error = router.select(args[1]); !error.empty()) {
            std::cout << error << std::endl;
            return 1;
        }
        for (const auto& s : router.speakers())  // one at a time
            if (connected(s) && &s != router.selected()) run({"bluetoothctl", "disconnect", s.mac}, 10s);
        const bool ok = connect(router.selected()->mac);
        std::cout << router.selected()->name << (ok ? " connected" : " didn't connect") << std::endl;
        return ok ? 0 : 1;
    }
    if (action == "disconnect") {
        for (const auto& s : router.speakers())
            if (connected(s)) {
                run({"bluetoothctl", "disconnect", s.mac}, 10s);
                std::cout << s.name << " disconnected" << std::endl;
            }
        return 0;
    }
    std::cerr << "unknown --cmd: " << action << std::endl;
    return 2;
}

// Scans 60 s for a speaker that isn't trusted yet, pairs, trusts and connects it, sets the alias.
int pair(const std::string& alias) {
    std::cout << "say \"Hey Google, pair Bluetooth\" to the Nest now; scanning 60 s…" << std::endl;
    std::vector<std::string> known;
    for (const auto& d : parse_devices(run({"bluetoothctl", "devices", "Trusted"}).out)) known.push_back(d.mac);
    std::atomic<pid_t> scan_pid{0};
    std::thread scan([&] {
        char* argv[] = {const_cast<char*>("bluetoothctl"), const_cast<char*>("--timeout"), const_cast<char*>("60"),
                        const_cast<char*>("scan"), const_cast<char*>("on"), nullptr};
        pid_t pid;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        if (posix_spawnp(&pid, "bluetoothctl", &actions, nullptr, argv, environ) == 0) {
            scan_pid = pid;
            waitpid(pid, nullptr, 0);
        }
        posix_spawn_file_actions_destroy(&actions);
    });
    std::optional<BtDevice> found;
    for (int i = 0; i < 30 && !found && running(); ++i) {
        buddy::sleep(2s);
        for (const auto& d : parse_devices(run({"bluetoothctl", "devices"}).out)) {
            if (std::ranges::find(known, d.mac) != known.end()) continue;
            if (parse_info(run({"bluetoothctl", "info", d.mac}).out).audio_sink) {
                found = d;
                break;
            }
            known.push_back(d.mac);  // not a speaker: don't ask again
        }
    }
    if (const pid_t p = scan_pid.load(); p > 0) kill(p, SIGTERM);
    scan.join();
    if (!found) {
        std::cout << "no new speaker found" << std::endl;
        return 1;
    }
    std::cout << "found \"" << found->name << "\" " << found->mac << std::endl;
    if (run({"bluetoothctl", "pair", found->mac}, 30s).out.find("Pairing successful") == std::string::npos) {
        std::cout << "pairing failed" << std::endl;
        return 1;
    }
    run({"bluetoothctl", "trust", found->mac});
    if (!alias.empty()) {
        // bluetoothctl's set-alias needs an interactive session; BlueZ's D-Bus property doesn't
        std::string path;
        std::string dev = "dev_" + found->mac;
        std::ranges::replace(dev, ':', '_');
        const auto tree = run({"busctl", "--system", "tree", "--list", "org.bluez"}).out;
        if (const auto at = tree.find("/" + dev); at != std::string::npos) {
            const auto start = tree.rfind('\n', at);
            path = tree.substr(start == std::string::npos ? 0 : start + 1, tree.find('\n', at) - (start == std::string::npos ? 0 : start + 1));
        }
        if (path.empty() || run({"busctl", "--system", "set-property", "org.bluez", path, "org.bluez.Device1", "Alias",
                                 "s", alias}).status != 0)
            std::cout << "couldn't set the alias" << std::endl;
    }
    const bool ok = connect(found->mac);
    std::cout << (alias.empty() ? found->name : alias) << (ok ? " paired and connected" : " paired, connect failed")
              << "; restart buddy@speakers to use it" << std::endl;
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();
    std::vector<std::string> args(argv + 1, argv + argc);
    const bool cmd = !args.empty() && args[0] == "--cmd" && args.size() >= 2;
    const bool pairing = !args.empty() && args[0] == "--pair" && args.size() <= 2;
    if (!args.empty() && !cmd && !pairing) {
        std::cerr << "usage: " << argv[0] << " [--cmd list | --cmd connect <name> | --cmd disconnect | --pair [alias]]"
                  << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[speakers] " << e.what() << std::endl;
        return 1;
    }
    if (cmd) return offline({args.begin() + 1, args.end()}, *cfg);
    if (pairing) return pair(args.size() == 2 ? args[1] : "");

    Router router(load_speakers(), cfg->router, cfg->default_speaker);
    std::cout << "[speakers] " << router.speakers().size() << " speaker(s)"
              << (router.selected() ? ", default " + router.selected()->name : "") << std::endl;

    BlockingQueue<Event> queue;
    on_stop([&] { queue.close(); });
    Mqtt mqtt("speakers", mqtt_config_from_env());
    mqtt.subscribe(topic::speakers_cmd, [&](const std::string&, const std::string& payload) {
        auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.is_object() || !msg.contains("action") || !msg["action"].is_string()) {
            std::cout << "[speakers] bad command: " << payload << std::endl;
            return;
        }
        queue.push({Event::Kind::Command, false, std::move(msg)});
    });
    auto state_of = [](const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        return msg.is_object() ? msg.value("state", "") : std::string();
    };
    mqtt.subscribe(topic::ears_state, [&](const std::string&, const std::string& payload) {
        const auto s = state_of(payload);
        queue.push({Event::Kind::Listening, s == "listening" || s == "transcribing"});
    });
    mqtt.subscribe(topic::voice_state, [&](const std::string&, const std::string& payload) {
        queue.push({Event::Kind::Speaking, state_of(payload) == "speaking"});
    });
    if (!mqtt.start()) return 0;

    PulseWatch watch([&] { queue.push({Event::Kind::Pulse}); });
    Snapshot snap = read_pipewire(router.speakers());
    std::string published;
    auto last_reload = std::chrono::steady_clock::now();
    while (running()) {
        systemd::notify_watchdog();
        auto timeout = std::chrono::milliseconds(5000);
        if (const auto d = router.deadline()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*d - Clock::now());
            timeout = std::clamp(left + 10ms, 0ms, timeout);
        }
        bool pulse = false;
        auto handle = [&](Event& e) {
            switch (e.kind) {
            case Event::Kind::Pulse: pulse = true; break;
            case Event::Kind::Listening: router.set_listening(e.on); break;
            case Event::Kind::Speaking: router.set_speaking(e.on); break;
            case Event::Kind::Command: {
                const std::string action = e.cmd["action"];
                const std::string speaker = e.cmd.value("speaker", "");
                std::string error;
                if (action == "output") error = router.select(speaker);
                else if (action == "connect") error = router.connect(speaker.empty() && router.selected()
                                                                         ? router.selected()->id : speaker, Clock::now());
                else if (action == "disconnect") router.disconnect();
                else error = "unknown action " + action;
                if (!error.empty()) std::cout << "[speakers] " << e.cmd.dump() << ": " << error << std::endl;
                break;
            }
            }
        };
        for (auto ev = queue.pop(timeout); ev; ev = queue.pop(0ms)) handle(*ev);
        // PipeWire changes come in bursts (a connect: card, sink, ports, volume): one read for all of them
        if (pulse)
            for (auto ev = queue.pop(30ms); ev; ev = queue.pop(30ms)) handle(*ev);
        if (!running()) break;
        if (router.speakers().empty() && std::chrono::steady_clock::now() - last_reload > 60s) {  // bluetoothd late
            last_reload = std::chrono::steady_clock::now();
            if (auto list = load_speakers(); !list.empty()) router = Router(std::move(list), cfg->router, cfg->default_speaker);
        }
        if (pulse) snap = read_pipewire(router.speakers());
        for (const auto& mac : execute(router.step(snap.world, Clock::now()), snap, router)) {
            router.connect_failed(mac);
            if (const Speaker* s = router.find(mac))
                mqtt.publish(topic::voice_say, json{{"text", "I can't reach the " + s->name + "."}}.dump(), 1);
        }
        if (auto state = router.state().dump(); state != published) {
            mqtt.publish(topic::speakers_state, state, 1, true);
            published = std::move(state);
        }
    }
    mqtt.stop();
    return 0;
}
