#pragma once
// The joint table: the only place that maps joint names to Pico channels and pulse widths. Callers speak degrees
// (0 = center); this file turns them into protocol v1 requests, clamped to each joint's range and slowed to its speed
// cap. Guide 2.3 "Joints, not channels".

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "buddy/motion_protocol.hpp"

namespace buddy::motion {

struct Joint {
    std::string_view name;
    int channel;
    uint16_t min_us, max_us;  // sent as LIMIT on every connect; the Pico enforces them too
    uint16_t center_us;       // 0°
    float us_per_deg;         // negative = joint turns the other way
    float max_deg_per_s;      // speed cap: moves that would be faster get a longer ramp

    float min_deg() const;
    float max_deg() const;
    float clamp(float deg) const;
    uint16_t to_us(float deg) const;  // clamped
};

std::span<const Joint> joints();
const Joint* find_joint(std::string_view name);

// Last commanded angle per joint; nullopt = relaxed or unknown (after a reconnect).
using Positions = std::map<std::string, std::optional<float>, std::less<>>;
Positions unknown_positions();  // every joint, all nullopt

inline constexpr int default_move_ms = 300;

struct MovePlan {
    std::vector<Request> requests;  // MOVE per joint, id 0 (the link assigns ids)
    uint16_t ms = 0;                // ramp time after the speed caps: every joint arrives together
    std::map<std::string, float> targets;  // clamped angles
};

// targets: joint → degrees. A joint at an unknown position counts as far away as its range allows (worst case for
// the cap). Error text for unknown joint names or an empty move.
std::variant<MovePlan, std::string> plan_move(const std::map<std::string, float>& targets, const Positions& from, int ms);

} // namespace buddy::motion
