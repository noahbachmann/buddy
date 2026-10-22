#include "buddy/mqtt.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "buddy/lifecycle.hpp"
#include "buddy/topics.hpp"

namespace buddy {

namespace {

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

    client_.set_connected_handler([this, status](const std::string&) {
        std::cout << "[" << service_ << "] connected to " << server_uri(cfg_) << std::endl;
        publish(status, "online", 1, true);
        for (const auto& [filter, handler] : subs_) client_.subscribe(filter, 1);
    });

    client_.set_connection_lost_handler([this](const std::string& cause) {
        std::cout << "[" << service_ << "] connection lost (" << cause << "), reconnecting" << std::endl;
    });

    client_.set_message_callback([this](mqtt::const_message_ptr msg) {
        const std::string& name = msg->get_topic();
        const std::string payload = msg->to_string();
        for (const auto& [filter, handler] : subs_) {
            if (topic::matches(filter, name)) handler(name, payload);
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

    while (running()) {
        try {
            client_.connect(opts)->wait();
            return true;
        } catch (const mqtt::exception& e) {
            std::cout << "[" << service_ << "] connect to " << server_uri(cfg_) << " failed: " << e.what()
                      << ", retrying in 2 s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    return false;
}

void Mqtt::publish(std::string_view topic, const std::string& payload, int qos, bool retained) {
    if (!client_.is_connected()) return;
    client_.publish(mqtt::make_message(std::string(topic), payload, qos, retained));
}

void Mqtt::stop() {
    if (!client_.is_connected()) return;
    client_.publish(mqtt::make_message(topic::status(service_), "offline", 1, true))->wait();
    client_.disconnect()->wait();
    std::cout << "[" << service_ << "] disconnected" << std::endl;
}

} // namespace buddy
