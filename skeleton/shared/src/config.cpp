#include "buddy/config.hpp"

#include <cstdlib>

namespace {

std::string env_or(const char* name, std::string fallback) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : fallback;
}

} // namespace

namespace buddy {

MqttConfig mqtt_config_from_env() {
    MqttConfig cfg;
    cfg.host = env_or("BUDDY_MQTT_HOST", cfg.host);
    cfg.port = std::stoi(env_or("BUDDY_MQTT_PORT", std::to_string(cfg.port)));
    cfg.user = env_or("BUDDY_MQTT_USER", "");
    cfg.password = env_or("BUDDY_MQTT_PASSWORD", "");
    return cfg;
}

} // namespace buddy
