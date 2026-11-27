// Watches the other services' status topics and turns trouble into notices (no I/O; main.cpp feeds it and the
// clock). The first "something important" Buddy reports by itself:
//
//   offline for `offline_after`           → alert "hue is offline", rewritten to "hue is back" when it returns
//   `kFlapCount` drops within kFlapWindow → alert "hue keeps restarting" (a crash loop is never offline for long)
//
// It watches a fixed list: retained statuses of services that were removed long ago still sit on the broker.
// Not covered: the Pi itself dying takes this watcher with it.
#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "command.hpp"

namespace buddy::messenger {

class Watch {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kFlapCount = 5;
    static constexpr std::chrono::minutes kFlapWindow{10};

    // offline_after 0 = no notices, only summary(). A service that never reports counts as offline since `now`.
    Watch(const std::vector<std::string>& services, std::chrono::seconds offline_after, Clock::time_point now);

    // From buddy/<service>/status; services not on the list are ignored.
    void status(const std::string& service, bool online, Clock::time_point now);

    // Notices due now. Call after status() and when deadline() is reached.
    std::vector<Notify> step(Clock::time_point now);
    std::optional<Clock::time_point> deadline() const;

    // One line for /status: "All 9 services online." / "Offline: hue, web. Online: 7."
    std::string summary() const;

private:
    struct Service {
        std::string name;
        bool online = false;
        Clock::time_point since;                // of `online`
        bool alerted = false;                   // "is offline" sent
        Clock::duration was_off{};              // length of the outage that just ended
        std::deque<Clock::time_point> drops;    // online → offline, within kFlapWindow
        bool flapping = false;                  // "keeps restarting" sent
    };

    std::vector<Service> services_;
    std::chrono::seconds offline_after_;
};

// "40 s", "12 min", "3 h 5 min"
std::string duration_text(std::chrono::seconds d);

} // namespace buddy::messenger
