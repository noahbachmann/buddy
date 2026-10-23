#include "buddy/mqtt.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "buddy/lifecycle.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"

namespace buddy {

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::string server_uri(const MqttConfig& cfg) {
    return "tcp://" + cfg.host + ":" + std::to_string(cfg.port);
}

} // namespace

Mqtt::Mqtt(std::string service, MqttConfig cfg)
    : service_(std::move(service)),
      cfg_(std::move(cfg)),
      client_(server_uri(cfg_), "buddy-" + service_) {}

Mqtt::~Mqtt() {
    try {
        stop();
    } catch (...) {
    }
}

void Mqtt::subscribe(std::string_view filter, Handler handler) {
    subs_.emplace_back(std::string(filter), std::move(handler));
}

bool Mqtt::start() {
    const std::string status = topic::status(service_);

    // Every service builds Mqtt after loading its models/config: from here on it only waits for the broker.
    systemd::notify_ready();

    client_.set_connected_handler([this, status](const std::string&) {
        std::cout << "[" << service_ << "] connected to " << server_uri(cfg_) << std::endl;
        systemd::notify_status("connected");
        publish(status, "online", 1, true);
        try {
            for (const auto& [filter, handler] : subs_) client_.subscribe(filter, 1);
        } catch (const mqtt::exception& e) {
            std::cout << "[" << service_ << "] subscribe failed: " << e.what() << std::endl;
        }
    });

    client_.set_connection_lost_handler([this](const std::string& cause) {
        std::cout << "[" << service_ << "] connection lost (" << cause << "), reconnecting" << std::endl;
        systemd::notify_status("reconnecting to the broker");
    });

    // Runs on Paho's thread: an exception escaping here would terminate the whole service.
    client_.set_message_callback([this](mqtt::const_message_ptr msg) {
        const std::string& name = msg->get_topic();
        const std::string payload = msg->to_string();
        for (const auto& [filter, handler] : subs_) {
            if (!topic::matches(filter, name)) continue;
            try {
                handler(name, payload);
            } catch (const std::exception& e) {
                std::cout << "[" << service_ << "] handler for " << name << " failed: " << e.what() << std::endl;
            }
        }
    });

    auto opts = mqtt::connect_options_builder()
                    .clean_session(true)
                    .keep_alive_interval(std::chrono::seconds(10))
                    .automatic_reconnect(std::chrono::seconds(1), std::chrono::seconds(30))
                    .will(mqtt::will_options(status, std::string("offline"), 1, true))
                    .finalize();
    if (!cfg_.user.empty()) {
        opts.set_user_name(cfg_.user);
        opts.set_password(cfg_.password);
    }

    // At boot the user units start ~6 s before Mosquitto: retry fast first, then back off. Log the first
    // failure and then only every 30 s (journald, microSD).
    auto delay = 250ms;
    auto next_log = Clock::now();
    while (running()) {
        systemd::notify_watchdog();
        try {
            client_.connect(opts)->wait();
            return true;
        } catch (const mqtt::exception& e) {
            if (Clock::now() >= next_log) {
                std::cout << "[" << service_ << "] connect to " << server_uri(cfg_) << " failed: " << e.what()
                          << ", retrying" << std::endl;
                systemd::notify_status("waiting for the broker");
                next_log = Clock::now() + 30s;
            }
            std::this_thread::sleep_for(delay);
            delay = std::min<std::chrono::milliseconds>(delay * 2, 5s);
        }
    }
    return false;
}

void Mqtt::publish(std::string_view topic, const std::string& payload, int qos, bool retained) {
    if (!client_.is_connected()) return;
    // The connection can drop between the check and the call; a lost event is fine, a crash isn't.
    try {
        client_.publish(mqtt::make_message(std::string(topic), payload, qos, retained));
    } catch (const mqtt::exception& e) {
        std::cout << "[" << service_ << "] publish to " << topic << " failed: " << e.what() << std::endl;
    }
}

void Mqtt::stop() {
    if (!client_.is_connected()) return;
    // Bounded waits: a broker that dies during shutdown mustn't hang `systemctl stop`.
    try {
        client_.publish(mqtt::make_message(topic::status(service_), "offline", 1, true))->wait_for(2s);
        client_.disconnect()->wait_for(2s);
        std::cout << "[" << service_ << "] disconnected" << std::endl;
    } catch (const mqtt::exception& e) {
        std::cout << "[" << service_ << "] disconnect failed: " << e.what() << std::endl;
    }
}

} // namespace buddy
