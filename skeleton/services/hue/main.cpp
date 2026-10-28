// hue: lights on the Philips Hue Bridge (local CLIP v2 API, no cloud), by voice and from the web panel.
//
//   buddy-hue                          live: buddy/hue/cmd in, retained buddy/hue/state + buddy/voice/say out.
//                                      The bridge's eventstream keeps the state current (Hue app, switches).
//   buddy-hue --pair                   once: press the bridge's link button, then run this within 30 s; saves the
//                                      key file. Finds the bridge via discovery.meethue.com, or BUDDY_HUE_BRIDGE.
//   buddy-hue --cmd <action> [query] [device]
//                                      test: runs one command and prints the reply (no MQTT)
//   buddy-hue --cmd list               test: rooms, zones, lights and scenes the bridge has (exact names)
//
// Config (env, see deploy/buddy.env.example): BUDDY_HUE_*
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "bridge.hpp"
#include "buddy/config.hpp"
#include "buddy/curl_http.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "home.hpp"
#include "lights.hpp"
#include "sse.hpp"
#include "throttle.hpp"

namespace {

using namespace buddy;
using namespace buddy::hue;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

const char* kNotPaired = "The lights aren't set up yet.";

struct Config {
    std::string key_file = env_string("BUDDY_HUE_KEY_FILE", env_string("HOME", ".") + "/buddy/config/hue.json");
    std::string bridge = env_string("BUDDY_HUE_BRIDGE", "");  // IP: overrides the key file's (DHCP moved it)
    LightsConfig lights{
        .default_room = env_string("BUDDY_HUE_DEFAULT_ROOM", ""),
        .step = env_int("BUDDY_HUE_STEP", 20),
    };
};

// Eventstream data (a JSON array of events), or a request to reload everything (stream reconnected).
struct Events {
    json data;
};
struct Reload {};
using Item = std::variant<Command, Events, Reload>;

// Reads the bridge's eventstream on its own thread and connection until running() is false. Reconnects with
// backoff 1 → 30 s; asks for a reload after each reconnect (events were missed meanwhile).
void read_events(const BridgeConfig& cfg, BlockingQueue<Item>& queue) {
    CurlHttp http(Bridge::curl_options(cfg));
    const Bridge bridge(cfg, http);
    auto headers = bridge.headers();
    headers.push_back("Accept: text/event-stream");
    auto backoff = std::chrono::seconds(1);
    bool connected_before = false, reported = false;
    while (running()) {
        if (connected_before) queue.push(Reload{});
        SseParser sse;
        const auto t0 = Clock::now();
        std::string error;
        try {
            const long status = http.stream(
                bridge.url("/eventstream/clip/v2"), headers,
                [&](std::string_view chunk) {
                    for (auto& data : sse.feed(chunk)) {
                        auto events = json::parse(data, nullptr, /*allow_exceptions=*/false);
                        if (events.is_array()) queue.push(Events{std::move(events)});
                    }
                },
                [] { return running(); });
            if (status != 200) error = "HTTP " + std::to_string(status);
        } catch (const HttpError& e) {
            error = e.what();
        }
        connected_before = true;
        if (!running()) break;
        if (Clock::now() - t0 > std::chrono::minutes(1)) backoff = std::chrono::seconds(1), reported = false;
        if (!reported) {  // once per outage: microSD
            std::cout << "[hue] eventstream lost" << (error.empty() ? "" : ": " + error) << ", reconnecting" << std::endl;
            reported = true;
        }
        for (auto until = Clock::now() + backoff; running() && Clock::now() < until;) std::this_thread::sleep_for(milliseconds(200));
        backoff = std::min(backoff * 2, std::chrono::seconds(30));
    }
}

int serve(const Config& cfg, const std::optional<BridgeConfig>& bridge_cfg) {
    BlockingQueue<Item> queue;  // HTTP runs on this thread, not on Paho's
    Mqtt mqtt("hue", mqtt_config_from_env());
    mqtt.subscribe(topic::hue_cmd, [&](const std::string&, const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.is_object() || !msg.contains("action") || !msg["action"].is_string() ||
            msg["action"] == "") {
            std::cout << "[hue] bad command: " << payload << std::endl;
            return;
        }
        auto text = [&](const char* key) { return msg.contains(key) && msg[key].is_string() ? msg[key].get<std::string>() : ""; };
        queue.push(Command{.action = msg["action"], .query = text("query"), .device = text("device"), .id = text("id")});
    });
    if (!bridge_cfg)
        std::cout << "[hue] not paired yet: press the bridge's link button, then run ~/buddy/bin/buddy-hue --pair"
                  << std::endl;
    if (!mqtt.start()) return 0;

    std::optional<CurlHttp> http;
    std::optional<Bridge> bridge;
    std::thread events;
    if (bridge_cfg) {
        http.emplace(Bridge::curl_options(*bridge_cfg));
        bridge.emplace(*bridge_cfg, *http);
        events = std::thread(read_events, *bridge_cfg, std::ref(queue));
    }

    Home home;
    Throttle throttle;
    std::string bridge_state = bridge ? "connecting" : "not paired";
    bool reload = bridge.has_value(), dirty = true;
    auto next_reload = Clock::now();
    auto last_publish = Clock::time_point{};
    std::string last_error;

    auto report = [&](const std::string& error) {  // log each distinct error once
        if (error != last_error) std::cout << "[hue] " << error << std::endl;
        last_error = error;
    };
    auto set_bridge_state = [&](const std::string& s) {
        if (s != bridge_state) dirty = true;
        bridge_state = s;
    };
    auto load = [&] {
        try {
            home.load(bridge->resources());
            reload = false;
            dirty = true;
            set_bridge_state("ok");
            if (!last_error.empty()) std::cout << "[hue] bridge ok" << std::endl;
            last_error.clear();
        } catch (const BridgeError& e) {
            report(e.what());
            set_bridge_state(e.status == 403 ? "key revoked" : "error");
            next_reload = Clock::now() + std::chrono::seconds(10);
        } catch (const HttpError& e) {
            report(e.what());
            set_bridge_state("unreachable");
            next_reload = Clock::now() + std::chrono::seconds(10);
        }
    };
    auto say = [&](const std::string& text) {
        if (!text.empty()) mqtt.publish(topic::voice_say, json{{"text", text}}.dump(), 1);
    };

    while (running()) {
        systemd::notify_watchdog();
        const auto now = Clock::now();
        auto timeout = milliseconds(200);
        auto until = [&](Clock::time_point t) {
            timeout = std::clamp(std::chrono::duration_cast<milliseconds>(t - now), milliseconds(0), timeout);
        };
        if (const auto t = throttle.next()) until(*t);
        if (dirty) until(last_publish + milliseconds(200));
        auto item = queue.pop(timeout);

        if (item) {
            if (auto* e = std::get_if<Events>(&*item)) {
                if (home.apply(e->data)) reload = true;
                dirty = true;
            } else if (std::holds_alternative<Reload>(*item)) {
                reload = true;
                next_reload = Clock::now();
            } else if (auto* c = std::get_if<Command>(&*item)) {
                if (!bridge) {
                    say(kNotPaired);
                    continue;
                }
                if (reload && !home.loaded()) load();  // first command while the bridge was away: try now
                const Plan p = plan(*c, home, cfg.lights);
                std::cout << "[hue] " << c->action << (c->query.empty() ? "" : " \"" + c->query + "\"")
                          << (c->device.empty() ? "" : " in \"" + c->device + "\"") << (c->id.empty() ? "" : " #" + c->id)
                          << (p.ok ? " ok" : " failed") << std::endl;
                say(p.say);
                for (const auto& r : p.requests) throttle.push(r);
            }
        }
        if (bridge && reload && Clock::now() >= next_reload) load();

        for (const auto& r : throttle.due(Clock::now())) {
            try {
                bridge->put(r);
            } catch (const BridgeError& e) {
                report(e.what());
                say(e.status == 403 ? "The Hue bridge doesn't know me anymore." : "The Hue bridge said no.");
            } catch (const HttpError& e) {
                report(e.what());
                set_bridge_state("unreachable");
                say("I can't reach the Hue bridge.");
            }
        }

        if (dirty && Clock::now() - last_publish >= milliseconds(200)) {
            json state = home.loaded() ? home.state() : json{{"rooms", json::array()}, {"lights", json::array()}};
            state["bridge"] = bridge_state;
            mqtt.publish(topic::hue_state, state.dump(), 1, true);
            last_publish = Clock::now();
            dirty = false;
        }
    }

    if (events.joinable()) events.join();  // stream() polls running() about once a second
    mqtt.stop();
    return 0;
}

int pair(const Config& cfg) {
    CurlHttp http;  // discovery and /api/config: public CA / plain HTTP
    BridgeConfig b;
    if (!cfg.bridge.empty()) {
        b.address = cfg.bridge;
        b.id = bridge_id_at(http, b.address);
    } else {
        const auto found = discover(http);
        if (found.empty()) throw std::runtime_error("no bridge found: set BUDDY_HUE_BRIDGE to its IP (router or Hue app)");
        if (found.size() > 1) {
            for (const auto& f : found) std::cout << "  " << f.id << " at " << f.address << std::endl;
            throw std::runtime_error("several bridges: set BUDDY_HUE_BRIDGE to one of the IPs above");
        }
        b = found[0];
    }
    std::cout << "bridge " << b.id << " at " << b.address << ": press its link button…" << std::endl;
    CurlHttp tls(Bridge::curl_options(b));
    Bridge bridge(b, tls);
    for (int i = 0; i < 30 && running(); ++i) {
        if (const auto key = bridge.pair("buddy#pi")) {
            b.app_key = *key;
            save_key_file(cfg.key_file, b);
            std::cout << "✓ paired, key saved to " << cfg.key_file << std::endl;
            std::cout << "  Tip: give the bridge a fixed IP in the router (DHCP reservation)." << std::endl;
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    throw std::runtime_error("link button not pressed within 30 s");
}

int list(Bridge& bridge) {
    Home home;
    home.load(bridge.resources());
    const json state = home.state();
    std::map<std::string, std::string> room_names;
    for (const auto& r : state["rooms"]) room_names[r["id"]] = r["name"];
    for (const auto& r : state["rooms"]) {
        std::cout << r["kind"].get<std::string>() << " \"" << r["name"].get<std::string>() << "\" "
                  << (r["on"].get<bool>() ? "on " + std::to_string(r["brightness"].get<int>()) + " %" : "off") << std::endl;
        for (const auto& s : r["scenes"]) std::cout << "    scene \"" << s["name"].get<std::string>() << "\"" << std::endl;
    }
    for (const auto& l : state["lights"]) {
        std::cout << "light \"" << l["name"].get<std::string>() << "\""
                  << (l["room"] == "" ? "" : " in \"" + room_names[l["room"]] + "\"")
                  << (l["color"].get<bool>() ? ", colour" : l["temperature"].get<bool>() ? ", white ambiance" : "")
                  << (l["on"].get<bool>() ? ", on " + std::to_string(l["brightness"].get<int>()) + " %" : ", off") << std::endl;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const bool pairing = argc == 2 && std::strcmp(argv[1], "--pair") == 0;
    const bool cmd = argc >= 3 && argc <= 5 && std::strcmp(argv[1], "--cmd") == 0;
    if (argc != 1 && !pairing && !cmd) {
        std::cerr << "usage: " << argv[0] << " [--pair | --cmd <action> [query] [device] | --cmd list]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[hue] " << e.what() << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int rc = 0;
    try {
        auto bridge_cfg = load_key_file(cfg->key_file);
        if (bridge_cfg && !cfg->bridge.empty()) bridge_cfg->address = cfg->bridge;
        if (pairing) {
            rc = pair(*cfg);
        } else if (cmd) {
            if (!bridge_cfg) throw std::runtime_error(std::string(kNotPaired) + " Run --pair first.");
            CurlHttp http(Bridge::curl_options(*bridge_cfg));
            Bridge bridge(*bridge_cfg, http);
            if (std::strcmp(argv[2], "list") == 0) {
                rc = list(bridge);
            } else {
                Home home;
                home.load(bridge.resources());
                const Plan p = plan({.action = argv[2], .query = argc >= 4 ? argv[3] : "", .device = argc == 5 ? argv[4] : ""},
                                    home, cfg->lights);
                for (const auto& r : p.requests) {
                    std::cout << "PUT " << r.type << "/" << r.id << " " << r.body.dump() << std::endl;
                    bridge.put(r);
                }
                std::cout << (p.ok ? "ok" : "failed") << (p.say.empty() ? "" : ", says: \"" + p.say + "\"") << std::endl;
                rc = p.ok ? 0 : 1;
            }
        } else {
            rc = serve(*cfg, bridge_cfg);
        }
    } catch (const std::exception& e) {
        std::cerr << "✗ " << e.what() << std::endl;
        rc = 1;
    }
    curl_global_cleanup();
    return rc;
}
