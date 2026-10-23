// Test service for step 1.5: receives pings (prints latency) and status changes of all services.
#include <chrono>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"

int main() {
    using namespace std::chrono;
    buddy::install_signal_handlers();

    buddy::Mqtt mqtt("pong", buddy::mqtt_config_from_env());

    mqtt.subscribe(buddy::topic::dev_ping, [](const std::string&, const std::string& payload) {
        const auto msg = nlohmann::json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.contains("n") || !msg.contains("ts_ms")) {
            std::cout << "[pong] bad payload: " << payload << std::endl;
            return;
        }
        const auto now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        std::cout << "[pong] received #" << msg["n"].get<long>() << ", latency "
                  << (now_ms - msg["ts_ms"].get<long long>()) << " ms" << std::endl;
    });

    mqtt.subscribe("buddy/+/status", [](const std::string& topic, const std::string& payload) {
        std::cout << "[pong] " << topic << " = " << payload << std::endl;
    });

    if (!mqtt.start()) return 0;
    while (buddy::running()) {
        buddy::systemd::notify_watchdog();
        std::this_thread::sleep_for(200ms);
    }

    mqtt.stop();
    return 0;
}
