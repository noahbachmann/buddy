#include <gtest/gtest.h>

#include "metrics.hpp"

using namespace buddy::system;

namespace {

const std::string kMeminfo =
    "MemTotal:        8255808 kB\n"
    "MemFree:         6968440 kB\n"
    "MemAvailable:    7492480 kB\n"
    "Buffers:           71264 kB\n";

const std::string kStat =
    "cpu  3229568 112 121580 154437363 17949 0 125762 0 0 0\n"
    "cpu0 575075 27 32468 38401407 2633 0 125048 0 0 0\n"
    "intr 331453679 0 0\n";

const std::string kNetdev =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier "
    "compressed\n"
    "    lo: 54628010  985940    0    0    0     0          0         0 54628010  985940    0    0    0     0       0  "
    "        0\n"
    "  eth0:    1000       10    0    0    0     0          0         0     2000      20    0    0    0     0       0  "
    "        0\n"
    " wlan0: 163286958  505182    0    4    0     0          0    117519 41530864  269448    0    0    0     0       0 "
    "         0\n";

TEST(Meminfo, Parses) {
    const auto m = parse_meminfo(kMeminfo);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->total_kb, 8255808);
    EXPECT_EQ(m->available_kb, 7492480);  // MemAvailable, not MemFree
}

TEST(Meminfo, MissingField) {
    EXPECT_FALSE(parse_meminfo("MemTotal: 100 kB\n"));
    EXPECT_FALSE(parse_meminfo(""));
}

TEST(Cpu, ParsesAggregateLine) {
    const auto t = parse_cpu(kStat);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->total, 3229568ULL + 112 + 121580 + 154437363 + 17949 + 0 + 125762 + 0);
    EXPECT_EQ(t->busy, t->total - 154437363 - 17949);
}

TEST(Cpu, RejectsGarbage) {
    EXPECT_FALSE(parse_cpu("cpu0 1 2 3 4 5 6 7 8\n"));
    EXPECT_FALSE(parse_cpu("cpu 1 2 3\n"));  // truncated
}

TEST(Cpu, Percent) {
    const CpuTimes prev{100, 1000};
    EXPECT_EQ(cpu_percent(prev, {150, 1100}), 50);
    EXPECT_EQ(cpu_percent(prev, {100, 1100}), 0);
    EXPECT_EQ(cpu_percent(prev, {200, 1100}), 100);
}

TEST(Cpu, PercentRejectsNoDeltaAndWrap) {
    const CpuTimes prev{100, 1000};
    EXPECT_FALSE(cpu_percent(prev, prev));        // first sample / stopped clock
    EXPECT_FALSE(cpu_percent(prev, {50, 900}));   // counter went backwards
    EXPECT_FALSE(cpu_percent(prev, {50, 1100}));  // busy wrapped alone
}

TEST(Net, SumsAllButLoopback) {
    const auto t = parse_netdev(kNetdev);
    EXPECT_EQ(t.rx, 1000 + 163286958ULL);
    EXPECT_EQ(t.tx, 2000 + 41530864ULL);
}

TEST(Net, Rate) {
    const auto r = net_rate({1000, 2000}, {6000, 4000}, 5.0);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->rx_bps, 1000);
    EXPECT_EQ(r->tx_bps, 400);
    EXPECT_FALSE(net_rate({1000, 2000}, {500, 4000}, 5.0));  // wrap
    EXPECT_FALSE(net_rate({1000, 2000}, {6000, 4000}, 0.0));
}

TEST(Throttled, Clean) {
    const auto t = parse_throttled("throttled=0x0\n");
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->now.empty());
    EXPECT_TRUE(t->ever.empty());
}

TEST(Throttled, ActiveAndLatched) {
    const auto t = parse_throttled("throttled=0x50005\n");  // bits 0,2 now; 16,18 ever
    ASSERT_TRUE(t);
    EXPECT_EQ(t->now, (std::vector<std::string>{"undervoltage", "throttled"}));
    EXPECT_EQ(t->ever, (std::vector<std::string>{"undervoltage", "throttled"}));
}

TEST(Throttled, LatchedOnly) {
    const auto t = parse_throttled("throttled=0xe0000\n");  // 17,18,19: capped, throttled, soft limit — all past
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->now.empty());
    EXPECT_EQ(t->ever, (std::vector<std::string>{"freq-capped", "throttled", "soft-temp-limit"}));
}

TEST(Throttled, Garbage) {
    EXPECT_FALSE(parse_throttled(""));
    EXPECT_FALSE(parse_throttled("error\n"));
}

TEST(Sanitize, Names) {
    EXPECT_EQ(sanitize_name("Pi 2"), "pi-2");
    EXPECT_EQ(sanitize_name("buddy"), "buddy");
    EXPECT_EQ(sanitize_name("  Living Room Pi  "), "living-room-pi");
    EXPECT_EQ(sanitize_name("!!!"), "host");
}

TEST(Snapshot, FullPayload) {
    Sample s;
    s.label = "Pi 1";
    s.cpu_pct = 12;
    s.cores = 4;
    s.mhz = 1500;
    s.max_mhz = 2400;
    s.mem_used_kb = 745472;   // 728 MB
    s.mem_total_kb = 8255808; // 8062 MB
    s.disk_used_b = 10200547328ULL;   // 10.2 GB
    s.disk_total_b = 125693590528ULL; // 125.7 GB
    s.temp_c = 52.3;
    s.fan_rpm = 2412;
    s.net = NetRate{12000, 800};
    s.throttled = Throttled{{}, {"undervoltage"}};
    const auto j = snapshot(s);
    EXPECT_EQ(j["label"], "Pi 1");
    EXPECT_EQ(j["cpu"]["pct"], 12);
    EXPECT_EQ(j["cpu"]["cores"], 4);
    EXPECT_EQ(j["cpu"]["mhz"], 1500);
    EXPECT_EQ(j["cpu"]["max_mhz"], 2400);
    EXPECT_EQ(j["mem"]["used_mb"], 728);
    EXPECT_EQ(j["mem"]["total_mb"], 8062);
    EXPECT_DOUBLE_EQ(j["disk"]["used_gb"].get<double>(), 10.2);
    EXPECT_DOUBLE_EQ(j["disk"]["total_gb"].get<double>(), 125.7);
    EXPECT_DOUBLE_EQ(j["temp_c"].get<double>(), 52.5);                 // 0.5 °C steps
    EXPECT_EQ(j["fan_rpm"], 2410);                       // 10 rpm steps
    EXPECT_EQ(j["net"]["rx_bps"], 12288);                // 1 KiB/s steps
    EXPECT_EQ(j["net"]["tx_bps"], 1024);
    EXPECT_EQ(j["throttled"]["now"].size(), 0u);
    EXPECT_EQ(j["throttled"]["ever"][0], "undervoltage");
}

TEST(Snapshot, OptionalFieldsOmitted) {
    Sample s;
    s.label = "buddy";
    s.cores = 4;
    const auto j = snapshot(s);
    EXPECT_FALSE(j["cpu"].contains("pct"));
    EXPECT_FALSE(j.contains("temp_c"));
    EXPECT_FALSE(j.contains("fan_rpm"));
    EXPECT_FALSE(j.contains("net"));
    EXPECT_FALSE(j.contains("throttled"));
}

// Publish-on-change: an idle host must produce byte-identical payloads despite tiny jitter.
TEST(Snapshot, RoundingIsStable) {
    Sample a, b;
    a.label = b.label = "buddy";
    a.cores = b.cores = 4;
    a.temp_c = 45.1;
    b.temp_c = 45.2;
    a.fan_rpm = 2408;
    b.fan_rpm = 2412;
    a.net = NetRate{100, 90};  // idle chatter under 1 KiB/s
    b.net = NetRate{400, 10};
    EXPECT_EQ(snapshot(a).dump(), snapshot(b).dump());
}

} // namespace
