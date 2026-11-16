#include <gtest/gtest.h>

#include "fleet.hpp"

using namespace buddy::messenger;

namespace {

// A real buddy/system/state payload (metrics.cpp snapshot())
const char* kPi =
    R"({"cpu":{"cores":4,"max_mhz":2400,"mhz":1700,"pct":3},"disk":{"total_gb":125.4,"used_gb":16.7},"fan_rpm":3100,)"
    R"("label":"buddy","mem":{"total_mb":8062,"used_mb":717},"net":{"rx_bps":3072,"tx_bps":1024},"temp_c":47.5,)"
    R"("throttled":{"ever":[],"now":[]}})";

} // namespace

TEST(Fleet, CondensesAStateIntoOneLine) {
    Fleet f;
    EXPECT_EQ(f.summary(), "");
    f.state("system", kPi);
    EXPECT_EQ(f.summary(), "🟢 buddy · CPU 3% · 47.5° · RAM 0.7/7.9G · 💾 17/125G");
}

TEST(Fleet, LatestStateWinsAndExtraHostsSortByLabel) {
    Fleet f;
    f.state("system-pi-2", R"({"label":"Pi 2","cpu":{"cores":4,"pct":80},"temp_c":60.0})");
    f.state("system", kPi);
    f.state("system", R"({"label":"buddy","cpu":{"cores":4,"pct":12},"temp_c":50.0})");
    EXPECT_EQ(f.summary(), "🟢 Pi 2 · CPU 80% · 60°\n🟢 buddy · CPU 12% · 50°");
}

TEST(Fleet, ThrottleFlagsAreShown) {
    Fleet f;
    f.state("system", R"({"label":"buddy","throttled":{"now":["under-voltage","soft temp limit"],"ever":["under-voltage"]}})");
    EXPECT_EQ(f.summary(), "🟢 buddy · ⚠ under-voltage, soft temp limit");
}

TEST(Fleet, OfflineHostShowsOfflineInsteadOfStaleNumbers) {
    Fleet f;
    f.state("system", kPi);
    f.status("system", false);
    EXPECT_EQ(f.summary(), "🔴 buddy · offline");
    f.status("system", true);
    EXPECT_EQ(f.summary(), "🟢 buddy · CPU 3% · 47.5° · RAM 0.7/7.9G · 💾 17/125G");
}

TEST(Fleet, IgnoresOtherServicesBadJsonAndUnknownStatus) {
    Fleet f;
    f.state("spotify", R"({"label":"not a host"})");
    f.state("system", "not json");
    f.state("system", R"({"cpu":{"pct":1}})");               // no label
    f.state("system", R"({"label":"buddy","mem":"wrong"})"); // field with the wrong shape
    f.status("system", false);                               // never reported: nothing to mark
    EXPECT_EQ(f.summary(), "");
    f.state("system", R"({"label":"buddy"})");               // minimal host: label only
    EXPECT_EQ(f.summary(), "🟢 buddy");
}
