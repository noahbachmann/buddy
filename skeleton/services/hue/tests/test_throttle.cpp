#include <gtest/gtest.h>

#include "throttle.hpp"

using namespace buddy::hue;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

Request req(const std::string& type, const std::string& id, json body) { return {.type = type, .id = id, .body = std::move(body)}; }

} // namespace

TEST(Throttle, FirstGoesAtOnceThenWaitsAndMerges) {
    Throttle t;
    const Throttle::Clock::time_point t0{std::chrono::hours(1)};
    t.push(req("grouped_light", "g", {{"on", {{"on", true}}}}));
    ASSERT_EQ(t.due(t0).size(), 1u);
    EXPECT_FALSE(t.next());

    t.push(req("grouped_light", "g", {{"on", {{"on", false}}}}));
    t.push(req("grouped_light", "g", {{"on", {{"on", true}}}, {"dimming", {{"brightness", 30}}}}));
    t.push(req("grouped_light", "g", {{"dimming", {{"brightness", 40}}}}));
    t.push(req("light", "l", {{"on", {{"on", true}}}}));
    const auto now = t.due(t0 + 100ms);  // the light goes, the group waits for its second
    ASSERT_EQ(now.size(), 1u);
    EXPECT_EQ(now[0].id, "l");
    EXPECT_EQ(t.next(), t0 + 1s);
    EXPECT_TRUE(t.due(t0 + 999ms).empty());
    const auto later = t.due(t0 + 1s);
    ASSERT_EQ(later.size(), 1u);
    EXPECT_EQ(later[0].body, (json{{"on", {{"on", true}}}, {"dimming", {{"brightness", 40}}}}));
}

TEST(Throttle, Intervals) {
    EXPECT_EQ(Throttle::interval("light"), 100ms);
    EXPECT_EQ(Throttle::interval("grouped_light"), 1000ms);
    EXPECT_EQ(Throttle::interval("scene"), 1000ms);
}
