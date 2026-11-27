#include <gtest/gtest.h>

#include "watch.hpp"

using namespace buddy::messenger;
using namespace std::chrono_literals;

namespace {

const Watch::Clock::time_point t0{};

Watch all_online(std::chrono::seconds offline_after = 300s) {
    Watch w({"hue", "web"}, offline_after, t0);
    w.status("hue", true, t0);
    w.status("web", true, t0);
    return w;
}

} // namespace

TEST(Watch, LongOutageAlertsOnceThenReportsTheReturn) {
    Watch w = all_online();
    EXPECT_EQ(w.deadline(), std::nullopt);
    w.status("hue", false, t0 + 10s);
    EXPECT_TRUE(w.step(t0 + 10s).empty());
    EXPECT_EQ(w.deadline(), t0 + 310s);
    EXPECT_TRUE(w.step(t0 + 309s).empty());

    const auto alerts = w.step(t0 + 310s);
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].text, "⚠ hue is offline");
    EXPECT_TRUE(alerts[0].alert);
    EXPECT_EQ(alerts[0].key, "watch:hue");
    EXPECT_TRUE(w.step(t0 + 900s).empty());
    EXPECT_EQ(w.deadline(), std::nullopt);

    w.status("hue", true, t0 + 910s);
    const auto back = w.step(t0 + 910s);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].text, "✓ hue is back (was off 15 min)");
    EXPECT_FALSE(back[0].alert);  // edits the alert, no second push
    EXPECT_EQ(back[0].key, "watch:hue");
    EXPECT_TRUE(w.step(t0 + 911s).empty());
}

TEST(Watch, RestartIsNoNews) {
    Watch w = all_online();
    w.status("web", false, t0 + 10s);
    w.status("web", true, t0 + 13s);
    EXPECT_TRUE(w.step(t0 + 13s).empty());
    EXPECT_TRUE(w.step(t0 + 400s).empty());
    EXPECT_EQ(w.deadline(), std::nullopt);
}

TEST(Watch, ServiceThatNeverShowsUpIsOffline) {
    Watch w({"hue", "web"}, 300s, t0);
    w.status("web", true, t0 + 1s);
    w.status("hue", false, t0 + 2s);  // stale retained status: the outage still counts from the start
    EXPECT_EQ(w.deadline(), t0 + 300s);
    const auto alerts = w.step(t0 + 300s);
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].text, "⚠ hue is offline");
}

TEST(Watch, UnlistedServicesAreIgnored) {
    Watch w = all_online();
    w.status("ghost", false, t0);
    EXPECT_TRUE(w.step(t0 + 1000s).empty());
    EXPECT_EQ(w.summary(), "All 2 services online.");
}

TEST(Watch, CrashLoopAlertsAlthoughNeverOfflineForLong) {
    Watch w = all_online();
    auto t = t0;
    for (std::size_t i = 0; i < Watch::kFlapCount; ++i) {
        EXPECT_TRUE(w.step(t).empty());
        t += 20s;
        w.status("hue", false, t);
        w.status("hue", true, t + 2s);
    }
    const auto alerts = w.step(t + 2s);
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].text, "⚠ hue keeps restarting");
    EXPECT_TRUE(alerts[0].alert);
    EXPECT_TRUE(w.step(t + 60s).empty());

    // quiet for a whole window after the last drop
    EXPECT_EQ(w.deadline(), t + Watch::kFlapWindow);
    EXPECT_TRUE(w.step(t + Watch::kFlapWindow - 1s).empty());
    const auto calm = w.step(t + Watch::kFlapWindow);
    ASSERT_EQ(calm.size(), 1u);
    EXPECT_EQ(calm[0].text, "✓ hue runs steadily again");
    EXPECT_FALSE(calm[0].alert);
    EXPECT_EQ(w.deadline(), std::nullopt);
}

TEST(Watch, OffSwitch) {
    Watch w = all_online(0s);
    w.status("hue", false, t0);
    EXPECT_TRUE(w.step(t0 + 9999s).empty());
    EXPECT_EQ(w.deadline(), std::nullopt);
    EXPECT_EQ(w.summary(), "Offline: hue. Online: 1.");
}

TEST(Watch, DurationText) {
    EXPECT_EQ(duration_text(40s), "40 s");
    EXPECT_EQ(duration_text(719s), "11 min");
    EXPECT_EQ(duration_text(3h), "3 h");
    EXPECT_EQ(duration_text(3h + 5min + 3s), "3 h 5 min");
}
