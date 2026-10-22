#pragma once
// Thin wrapper around the Paho async client:
//  - client id "buddy-<service>": only one instance per service can be connected
//  - last will: buddy/<service>/status = "offline" (retained)
//  - on every (re)connect: publishes "online" and (re)subscribes
//  - automatic reconnect; the first connect retries until the broker is reachable

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mqtt/async_client.h>

#include "buddy/config.hpp"

namespace buddy {

class Mqtt {
public:
    using Handler = std::function<void(const std::string& topic, const std::string& payload)>;

    Mqtt(std::string service, MqttConfig cfg);
    ~Mqtt();
    Mqtt(const Mqtt&) = delete;
    Mqtt& operator=(const Mqtt&) = delete;

    // Register before start(). Filters may use MQTT wildcards (+, #).
    void subscribe(std::string_view filter, Handler handler);

    // Blocks until connected or shutdown was requested. Returns false on shutdown.
    bool start();

    // Messages are dropped while disconnected (fine for events; revisit for commands).
    void publish(std::string_view topic, const std::string& payload, int qos = 0, bool retained = false);

    void stop();

private:
    std::string service_;
    MqttConfig cfg_;
    mqtt::async_client client_;
    std::vector<std::pair<std::string, Handler>> subs_;
};

} // namespace buddy
