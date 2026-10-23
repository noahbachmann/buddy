// Test service for step 1.5: publishes a counter + timestamp once per second.
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

    buddy::Mqtt mqtt("ping", buddy::mqtt_config_from_env());
    if (!mqtt.start()) return 0;

    for (long n = 0; buddy::running(); ++n) {
        const auto ts_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        const nlohmann::json msg{{"n", n}, {"ts_ms", ts_ms}};
        mqtt.publish(buddy::topic::dev_ping, msg.dump());
        if (n % 10 == 0) std::cout << "[ping] sent #" << n << std::endl;
        buddy::systemd::notify_watchdog();
        std::this_thread::sleep_for(1s);
    }

    mqtt.stop();
    return 0;
}
