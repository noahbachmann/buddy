#include "joints.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace buddy::motion {

namespace {

// ⚠️ values: measured in 2.5 once the servos sit in the head.
constexpr std::array table = {
    // Demo servo on ch0 for "cut off Andi's balls" (gestures.cpp), not a body joint: head_pan takes ch0 in 2.5 and this
    // row goes together with the gesture. ±80° on an MG90S without a horn ⚠️.
    Joint{.name = "demo", .channel = 0, .min_us = 700, .max_us = 2300, .center_us = 1500, .us_per_deg = 10,
          .max_deg_per_s = 400},
    Joint{.name = "head_tilt", .channel = 1, .min_us = 1100, .max_us = 1900, .center_us = 1500, .us_per_deg = 10,
          .max_deg_per_s = 90},
};

} // namespace

float Joint::min_deg() const {
    return std::min((min_us - center_us) / us_per_deg, (max_us - center_us) / us_per_deg);
}

float Joint::max_deg() const {
    return std::max((min_us - center_us) / us_per_deg, (max_us - center_us) / us_per_deg);
}

float Joint::clamp(float deg) const { return std::clamp(deg, min_deg(), max_deg()); }

uint16_t Joint::to_us(float deg) const {
    const long us = std::lround(center_us + clamp(deg) * us_per_deg);
    return static_cast<uint16_t>(std::clamp<long>(us, min_us, max_us));  // rounding can't step outside
}

std::span<const Joint> joints() { return table; }

const Joint* find_joint(std::string_view name) {
    for (const auto& j : table)
        if (j.name == name) return &j;
    return nullptr;
}

Positions unknown_positions() {
    Positions p;
    for (const auto& j : table) p.emplace(std::string(j.name), std::nullopt);
    return p;
}

std::variant<MovePlan, std::string> plan_move(const std::map<std::string, float>& targets, const Positions& from, int ms) {
    if (targets.empty()) return std::string("no joints");
    MovePlan plan;
    int need_ms = std::clamp(ms, 0, int(max_ramp_ms));
    for (const auto& [name, deg] : targets) {
        const Joint* j = find_joint(name);
        if (!j) return "unknown joint " + name;
        if (!std::isfinite(deg)) return "bad angle for " + name;
        const float target = j->clamp(deg);
        const auto at = from.find(name);
        const float distance = at != from.end() && at->second ? std::abs(target - *at->second)
                                                               : std::max(target - j->min_deg(), j->max_deg() - target);
        need_ms = std::max(need_ms, int(std::ceil(distance / j->max_deg_per_s * 1000.0f)));
        plan.targets[name] = target;
    }
    plan.ms = static_cast<uint16_t>(std::min(need_ms, int(max_ramp_ms)));
    for (const auto& [name, deg] : plan.targets) {
        const Joint* j = find_joint(name);
        plan.requests.push_back({.op = Op::move, .channel = j->channel, .a = j->to_us(deg), .b = plan.ms});
    }
    return plan;
}

} // namespace buddy::motion
