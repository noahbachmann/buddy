#include <gtest/gtest.h>

#include <limits>

#include "joints.hpp"

using namespace buddy::motion;

namespace {

MovePlan plan(const std::map<std::string, float>& targets, const Positions& from, int ms) {
    auto p = plan_move(targets, from, ms);
    EXPECT_TRUE(std::holds_alternative<MovePlan>(p)) << std::get<std::string>(p);
    return std::get<MovePlan>(p);
}

Positions at(std::map<std::string, float> known) {
    auto p = unknown_positions();
    for (const auto& [name, deg] : known) p[name] = deg;
    return p;
}

} // namespace

TEST(MotionJoints, TableFitsTheProtocol) {
    for (const auto& j : joints()) {
        EXPECT_GE(j.min_us, hard_min_us) << j.name;
        EXPECT_LE(j.max_us, hard_max_us) << j.name;
        EXPECT_LT(j.min_us, j.max_us) << j.name;
        EXPECT_TRUE(j.min_us <= j.center_us && j.center_us <= j.max_us) << j.name;
        EXPECT_GT(j.max_deg_per_s, 0) << j.name;
        EXPECT_EQ(find_joint(j.name), &j);
        for (const auto& other : joints()) {
            if (&other == &j) continue;
            EXPECT_NE(other.channel, j.channel) << j.name << " and " << other.name;
        }
    }
}

TEST(MotionJoints, DegreesToPulse) {
    const Joint* demo = find_joint("demo");
    ASSERT_NE(demo, nullptr);
    EXPECT_EQ(demo->to_us(0), 1500);
    EXPECT_EQ(demo->to_us(30), 1800);
    EXPECT_EQ(demo->to_us(-80), 700);
    EXPECT_EQ(demo->to_us(200), 2300);  // clamped at both ends
    EXPECT_EQ(demo->to_us(-200), 700);
    EXPECT_FLOAT_EQ(demo->clamp(200), 80);

    const Joint reversed{.name = "r", .channel = 9, .min_us = 1000, .max_us = 2000, .center_us = 1500,
                         .us_per_deg = -10, .max_deg_per_s = 100};
    EXPECT_EQ(reversed.to_us(30), 1200);
    EXPECT_FLOAT_EQ(reversed.min_deg(), -50);
    EXPECT_FLOAT_EQ(reversed.max_deg(), 50);
}

TEST(MotionJoints, MoveRequests) {
    const auto p = plan({{"demo", 30}}, at({{"demo", 0}}), 500);
    ASSERT_EQ(p.requests.size(), 1u);
    EXPECT_EQ(p.requests[0], (Request{.op = Op::move, .channel = 0, .a = 1800, .b = 500}));
    EXPECT_FLOAT_EQ(p.targets.at("demo"), 30);
    EXPECT_FLOAT_EQ(plan({{"demo", 200}}, at({{"demo", 0}}), 500).targets.at("demo"), 80);
}

TEST(MotionJoints, SpeedCapStretchesTheRamp) {
    // demo: 400°/s → 160° need 400 ms; head_tilt: 90°/s → 40° need 445 ms
    EXPECT_EQ(plan({{"demo", 80}}, at({{"demo", -80}}), 50).ms, 400);
    EXPECT_EQ(plan({{"demo", 80}}, at({{"demo", -80}}), 1000).ms, 1000);  // slower is fine
    EXPECT_EQ(plan({{"head_tilt", 20}}, at({{"head_tilt", -20}}), 0).ms, 445);
    // unknown position: worst case over the range (the servo could be at the far end)
    EXPECT_EQ(plan({{"demo", 0}}, unknown_positions(), 0).ms, 200);
    EXPECT_EQ(plan({{"demo", 80}}, unknown_positions(), 0).ms, 400);
}

TEST(MotionJoints, MultiJointMovesArriveTogether) {
    const auto p = plan({{"demo", -30}, {"head_tilt", 10}}, at({{"demo", 0}, {"head_tilt", -10}}), 50);
    ASSERT_EQ(p.requests.size(), 2u);
    EXPECT_EQ(p.ms, 223);  // head_tilt's 20° at 90°/s is the slowest
    for (const auto& r : p.requests) EXPECT_EQ(r.b, p.ms);
}

TEST(MotionJoints, RampNeverExceedsTheProtocol) {
    EXPECT_EQ(plan({{"demo", 10}}, at({{"demo", 0}}), 60000).ms, max_ramp_ms);
}

TEST(MotionJoints, BadMoves) {
    EXPECT_TRUE(std::holds_alternative<std::string>(plan_move({{"tail", 10}}, unknown_positions(), 300)));
    EXPECT_TRUE(std::holds_alternative<std::string>(plan_move({}, unknown_positions(), 300)));
    EXPECT_TRUE(std::holds_alternative<std::string>(plan_move({{"demo", std::numeric_limits<float>::quiet_NaN()}}, unknown_positions(), 300)));
}
