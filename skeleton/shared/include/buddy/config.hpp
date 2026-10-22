#pragma once
#include <string>

namespace buddy {

// Read from the environment (systemd loads ~/buddy/config/buddy.env).
// Only the broker address changes when a service moves to another host (D3).
struct MqttConfig {
    std::string host = "localhost";  // BUDDY_MQTT_HOST
    int port = 1883;                 // BUDDY_MQTT_PORT
    std::string user;                // BUDDY_MQTT_USER     (optional)
    std::string password;            // BUDDY_MQTT_PASSWORD (optional)
};

MqttConfig mqtt_config_from_env();

} // namespace buddy
