#pragma once
// Keeps PUTs to the bridge within its limits (Hue: about 10/s to lights, 1/s to groups ⚠️ from Hue's guidance,
// not measured). A request to a resource that was just sent waits; requests that pile up for one resource merge
// (JSON merge patch, later fields win), so a slider sends its last value and "off" then "50 %" ends at 50 %.
// Merged "brighter" deltas don't add up: a voice command never comes that fast.

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bridge.hpp"

namespace buddy::hue {

class Throttle {
public:
    using Clock = std::chrono::steady_clock;

    void push(Request r);
    // Requests whose resource may be sent to at `now`, marked as sent.
    std::vector<Request> due(Clock::time_point now);
    // When the next waiting request becomes due; nullopt = none waiting.
    std::optional<Clock::time_point> next() const;

    static Clock::duration interval(const std::string& type);

private:
    std::map<std::string, Request> pending_;              // by "type/id"
    std::map<std::string, Clock::time_point> last_sent_;  // by "type/id"
};

} // namespace buddy::hue
