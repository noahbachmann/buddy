#include <gtest/gtest.h>

#include "gestures.hpp"
#include "joints.hpp"

using namespace buddy::motion;
using namespace std::chrono_literals;
using Clock = GesturePlayer::Clock;

TEST(MotionGestures, TableUsesRealJoints) {
    for (const auto& g : gestures()) {
        EXPECT_FALSE(g.reply.empty()) << g.name;
        EXPECT_FALSE(g.steps.empty()) << g.name;
        for (const auto& s : g.steps) {
            for (const auto& [name, deg] : s.joints) {
                const Joint* j = find_joint(name);
                ASSERT_NE(j, nullptr) << g.name << ": " << name;
                EXPECT_FLOAT_EQ(j->clamp(deg), deg) << g.name << ": " << name << " outside its range";
            }
            EXPECT_TRUE(s.relax || !s.joints.empty()) << g.name;
        }
        EXPECT_TRUE(g.steps.back().relax) << g.name << " must end limp";
    }
}

TEST(MotionGestures, Shake) {  // "cut off Andi's balls": left, right, left, right, center, relax
    const Gesture* g = find_gesture("shake");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->reply, "As you wish.");
    EXPECT_EQ(g->joint_names(), std::set<std::string>{"demo"});
    std::vector<float> path;
    for (const auto& s : g->steps)
        if (!s.relax) path.push_back(s.joints.at("demo"));
    EXPECT_EQ(path, (std::vector<float>{-80, 80, -80, 80, 0}));
    EXPECT_EQ(find_gesture("moonwalk"), nullptr);
}

TEST(MotionGestures, PlayerWaitsForEachRamp) {
    const Gesture g{.name = "g", .reply = "ok", .steps = {{{{"demo", 10}}, 100}, {{{"demo", 20}}, 100}, {{}, 0, true}}};
    GesturePlayer p;
    const auto t0 = Clock::now();
    EXPECT_FALSE(p.next_at());
    p.start(g, t0);
    ASSERT_TRUE(p.active());
    EXPECT_EQ(p.next_at(), t0);

    const Step* s = p.due(t0);
    ASSERT_EQ(s, &g.steps[0]);
    p.wait(150ms, t0);  // stretched by a speed cap
    EXPECT_EQ(p.due(t0 + 100ms), nullptr);
    EXPECT_EQ(p.next_at(), t0 + 150ms);
    ASSERT_EQ(p.due(t0 + 150ms), &g.steps[1]);
    p.wait(100ms, t0 + 150ms);
    ASSERT_EQ(p.due(t0 + 250ms), &g.steps[2]);
    p.wait(0ms, t0 + 250ms);
    EXPECT_EQ(p.due(t0 + 250ms), nullptr);
    EXPECT_FALSE(p.active());
}

TEST(MotionGestures, Cancel) {
    GesturePlayer p;
    p.start(*find_gesture("shake"), Clock::now());
    p.cancel();
    EXPECT_FALSE(p.active());
    EXPECT_EQ(p.due(Clock::now()), nullptr);
}
