#pragma once
// Named gestures: timed sequences of multi-joint moves, played by motion itself so their timing doesn't depend on
// MQTT latency. A gesture also carries the reply Buddy speaks when it starts (D11: motion knows the outcome).

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace buddy::motion {

struct Step {
    std::map<std::string, float> joints{};  // degrees; empty + relax = relax the gesture's joints
    int ms = 0;                           // ramp; the next step starts when this one has arrived
    bool relax = false;
};

struct Gesture {
    std::string name;
    std::string reply;
    std::vector<Step> steps;

    std::set<std::string> joint_names() const;  // every joint a step moves
};

std::span<const Gesture> gestures();
const Gesture* find_gesture(std::string_view name);

// Plays one gesture at a time: the caller asks due() in its loop, executes the step and reports the actual ramp
// (after speed caps) with wait(). No I/O, no threads.
class GesturePlayer {
public:
    using Clock = std::chrono::steady_clock;

    void start(const Gesture& g, Clock::time_point now);
    void cancel() { gesture_ = nullptr; }
    bool active() const { return gesture_ != nullptr; }
    const Gesture* gesture() const { return gesture_; }

    // The next step if its time has come (advances), else nullptr. After the last step's wait: inactive.
    const Step* due(Clock::time_point now);
    // The step just returned by due() takes this long (its ms, maybe stretched by the speed cap).
    void wait(std::chrono::milliseconds ramp, Clock::time_point now) { next_at_ = now + ramp; }
    // When due() returns something next (for the main loop's timeout); nullopt = inactive.
    std::optional<Clock::time_point> next_at() const;

private:
    const Gesture* gesture_ = nullptr;
    std::size_t next_ = 0;
    Clock::time_point next_at_{};
};

} // namespace buddy::motion
