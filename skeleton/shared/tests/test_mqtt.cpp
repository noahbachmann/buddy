// Needs a broker on localhost:1883 (the dev VM has Mosquitto); skipped otherwise. Same as test_mqtt.py.
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "buddy/mqtt.hpp"

using namespace std::chrono_literals;

namespace {

bool broker_up() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1883);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool up = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
    close(fd);
    return up;
}

// Plain Paho, not buddy::Mqtt: that would leave its own retained status behind. Else the web panel shows test clients.
void clear_retained(const std::vector<std::string>& topics) {
    mqtt::async_client c("tcp://localhost:1883", "buddy-gtest-cleaner", mqtt::create_options(MQTTVERSION_3_1_1));
    c.connect()->wait_for(3s);
    for (const auto& t : topics) c.publish(mqtt::make_message(t, "", 1, true))->wait_for(2s);
    c.disconnect()->wait_for(2s);
}

// Messages from Paho's thread, waited for on the test thread.
struct Inbox {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::pair<std::string, std::string>> got;

    void add(const std::string& topic, const std::string& payload) {
        std::lock_guard lock(m);
        got.emplace_back(topic, payload);
        cv.notify_all();
    }
    bool wait_for(const std::string& topic, const std::string& payload, std::chrono::milliseconds timeout) {
        std::unique_lock lock(m);
        return cv.wait_for(lock, timeout, [&] {
            return std::find(got.begin(), got.end(), std::pair(topic, payload)) != got.end();
        });
    }
};

} // namespace

TEST(Mqtt, StatusRoundTripAndHandlerErrors) {
    if (!broker_up()) GTEST_SKIP() << "no MQTT broker on localhost:1883";
    Inbox inbox;
    buddy::Mqtt watcher("gtest-watcher", {});
    watcher.subscribe("buddy/gtest-svc/#", [&](const auto& t, const auto& p) { inbox.add(t, p); });
    watcher.subscribe("buddy/gtest-svc/#", [](const auto&, const auto&) { throw std::runtime_error("boom"); });
    watcher.subscribe("buddy/gtest-watcher/probe", [&](const auto& t, const auto& p) { inbox.add(t, p); });
    ASSERT_TRUE(watcher.start());
    // start() returns before the broker acks the subscriptions: the probe coming back proves they're active
    bool subscribed = false;
    for (int i = 0; i < 30 && !subscribed; ++i) {
        watcher.publish("buddy/gtest-watcher/probe", "1");
        subscribed = inbox.wait_for("buddy/gtest-watcher/probe", "1", 100ms);
    }
    ASSERT_TRUE(subscribed);

    {
        buddy::Mqtt svc("gtest-svc", {});
        ASSERT_TRUE(svc.start());
        EXPECT_TRUE(inbox.wait_for("buddy/gtest-svc/status", "online", 3s));
        svc.publish("buddy/gtest-svc/event", R"({"x":1})");
        EXPECT_TRUE(inbox.wait_for("buddy/gtest-svc/event", R"({"x":1})", 3s));
        svc.stop();
    }
    EXPECT_TRUE(inbox.wait_for("buddy/gtest-svc/status", "offline", 3s));  // also proves the throwing handler didn't kill Paho
    watcher.stop();
    clear_retained({"buddy/gtest-svc/status", "buddy/gtest-watcher/status"});
}
