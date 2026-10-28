// web: Buddy's control panel for phone and PC on the LAN, http://buddy.local:8080. An HTTP ⇄ MQTT bridge with no
// skill logic: retained buddy/+/state and buddy/+/status go to the browser (server-sent events), the browser's
// commands go to buddy/<service>/cmd. The page itself is static (www/).
//
//   buddy-web     live
//
// Config (env, see deploy/buddy.env.example): BUDDY_WEB_*
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <httplib.h>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/systemd.hpp"
#include "server.hpp"
#include "state.hpp"

namespace {

using namespace buddy;
using namespace buddy::web;

std::vector<std::string> name_list(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream in(csv);
    for (std::string name; std::getline(in, name, ',');) {
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        if (!name.empty()) out.push_back(name);
    }
    return out;
}

struct Config {
    std::string bind = env_string("BUDDY_WEB_BIND", "0.0.0.0");
    int port = env_int("BUDDY_WEB_PORT", 8080);
    WebConfig web{
        .root = env_string("BUDDY_WEB_ROOT", env_string("HOME", ".") + "/buddy/share/web"),
        .extra_hosts = name_list(env_string("BUDDY_WEB_HOSTS", "")),
    };
};

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();
    if (argc != 1) {
        std::cerr << "usage: " << argv[0] << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[web] " << e.what() << std::endl;
        return 1;
    }

    StateStore store;
    Mqtt mqtt("web", mqtt_config_from_env());
    for (const char* filter : {"buddy/+/state", "buddy/+/status"})
        mqtt.subscribe(filter, [&](const std::string& topic, const std::string& payload) { store.set(topic, payload); });

    httplib::Server server;
    if (!add_routes(server, store, [&](const std::string& t, const std::string& p) { mqtt.publish(t, p, 1); }, cfg->web)) {
        std::cerr << "[web] " << cfg->web.root << " is not a directory (BUDDY_WEB_ROOT)" << std::endl;
        return 1;
    }
    if (!server.bind_to_port(cfg->bind, cfg->port)) {
        std::cerr << "[web] can't listen on " << cfg->bind << ":" << cfg->port << " (in use? BUDDY_WEB_PORT)" << std::endl;
        return 1;
    }
    if (!mqtt.start()) return 0;
    std::thread http([&] { server.listen_after_bind(); });
    std::cout << "[web] http://" << cfg->bind << ":" << cfg->port << " serving " << cfg->web.root << std::endl;

    while (running()) {
        systemd::notify_watchdog();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    store.close();  // ends the event streams
    server.stop();
    http.join();
    mqtt.stop();
    return 0;
}
