#pragma once
// Parsers and payload assembly for the host metrics the system service publishes (buddy/system/state).
// Pure text → values, no I/O: main.cpp reads /proc, /sys and vcgencmd and calls these, the tests feed
// fixture strings. snapshot() rounds every value so an idle host produces the same payload each sample
// (publish-on-change keeps the bus and journal quiet).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace buddy::system {

struct Meminfo {
    long total_kb = 0;
    long available_kb = 0;  // MemAvailable: what a new program could use, not MemFree
};
std::optional<Meminfo> parse_meminfo(const std::string& text);

// Aggregate "cpu " line of /proc/stat, in jiffies since boot.
struct CpuTimes {
    unsigned long long busy = 0;
    unsigned long long total = 0;
};
std::optional<CpuTimes> parse_cpu(const std::string& proc_stat);

// 0..100 over the sampling window; nullopt when the counters didn't move or went backwards (first sample, wrap).
std::optional<int> cpu_percent(const CpuTimes& prev, const CpuTimes& cur);

// Bytes since boot, every interface except lo summed (/proc/net/dev).
struct NetTotals {
    unsigned long long rx = 0;
    unsigned long long tx = 0;
};
NetTotals parse_netdev(const std::string& text);

struct NetRate {
    long rx_bps = 0;
    long tx_bps = 0;
};
std::optional<NetRate> net_rate(const NetTotals& prev, const NetTotals& cur, double dt_s);

// Decoded `vcgencmd get_throttled` bitfield: now = active conditions, ever = latched since boot.
struct Throttled {
    std::vector<std::string> now;
    std::vector<std::string> ever;
};
std::optional<Throttled> parse_throttled(const std::string& out);

// Display name → MQTT-safe service suffix: "Pi 2" → "pi-2". Lowercase a-z 0-9 and '-' only, never empty.
std::string sanitize_name(const std::string& name);

// One sample; optionals absent → field omitted from the payload.
struct Sample {
    std::string label;
    std::optional<int> cpu_pct;
    int cores = 0;
    std::optional<int> mhz;
    std::optional<int> max_mhz;
    long mem_used_kb = 0;
    long mem_total_kb = 0;
    std::uint64_t disk_used_b = 0;
    std::uint64_t disk_total_b = 0;
    std::optional<double> temp_c;
    std::optional<int> fan_rpm;
    std::optional<NetRate> net;
    std::optional<Throttled> throttled;
};

// Payload for buddy/system*/state. Rounds: temp 0.5 °C, disk 0.1 GB, fan 10 rpm, net 1 KiB/s.
nlohmann::json snapshot(const Sample& s);

} // namespace buddy::system
