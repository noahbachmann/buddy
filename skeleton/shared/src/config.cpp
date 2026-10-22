#include "buddy/config.hpp"

#include <cstdlib>
#include <stdexcept>

namespace {

template <typename T, typename Parse>
T env_number(const char* name, T fallback, Parse parse) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        std::size_t used = 0;
        const T result = parse(std::string(value), &used);
        if (value[used] == '\0') return result;
    } catch (const std::exception&) {
    }
    throw std::invalid_argument(std::string(name) + "=\"" + value + "\" is not a valid number");
}

} // namespace

namespace buddy {

std::string env_string(const char* name, std::string fallback) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : fallback;
}

int env_int(const char* name, int fallback) {
    return env_number(name, fallback, [](const std::string& s, std::size_t* used) { return std::stoi(s, used); });
}

float env_float(const char* name, float fallback) {
    return env_number(name, fallback, [](const std::string& s, std::size_t* used) { return std::stof(s, used); });
}

MqttConfig mqtt_config_from_env() {
    MqttConfig cfg;
    cfg.host = env_string("BUDDY_MQTT_HOST", cfg.host);
    cfg.port = env_int("BUDDY_MQTT_PORT", cfg.port);
    cfg.user = env_string("BUDDY_MQTT_USER", "");
    cfg.password = env_string("BUDDY_MQTT_PASSWORD", "");
    return cfg;
}

} // namespace buddy
