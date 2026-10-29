#include "gestures.hpp"

#include <vector>

namespace buddy::motion {

namespace {

const std::vector<Gesture>& table() {
    // Demo for friends ("Hey Jarvis, cut off Andi's balls"): remove together with the demo joint (joints.cpp) in 2.5.
    static const std::vector<Gesture> t = {
        {.name = "shake",
         .reply = "As you wish.",
         .steps = {{{{"demo", -80}}, 400},
                   {{{"demo", 80}}, 400},
                   {{{"demo", -80}}, 400},
                   {{{"demo", 80}}, 400},
                   {{{"demo", 0}}, 400},
                   {{}, 0, true}}},
    };
    return t;
}

} // namespace

std::set<std::string> Gesture::joint_names() const {
    std::set<std::string> names;
    for (const auto& s : steps)
        for (const auto& [name, deg] : s.joints) names.insert(name);
    return names;
}

std::span<const Gesture> gestures() { return table(); }

const Gesture* find_gesture(std::string_view name) {
    for (const auto& g : table())
        if (g.name == name) return &g;
    return nullptr;
}

void GesturePlayer::start(const Gesture& g, Clock::time_point now) {
    gesture_ = &g;
    next_ = 0;
    next_at_ = now;
}

const Step* GesturePlayer::due(Clock::time_point now) {
    if (!gesture_ || now < next_at_) return nullptr;
    if (next_ >= gesture_->steps.size()) {  // last step's ramp is over
        gesture_ = nullptr;
        return nullptr;
    }
    return &gesture_->steps[next_++];
}

std::optional<GesturePlayer::Clock::time_point> GesturePlayer::next_at() const {
    if (!gesture_) return std::nullopt;
    return next_at_;
}

} // namespace buddy::motion
