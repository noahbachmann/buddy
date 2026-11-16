#include "metrics.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace buddy::system {

namespace {

using json = nlohmann::json;

std::optional<long> field_kb(const std::string& text, const std::string& key) {
    const auto pos = text.find(key);
    if (pos == std::string::npos) return std::nullopt;
    return std::strtol(text.c_str() + pos + key.size(), nullptr, 10);
}

} // namespace

std::optional<Meminfo> parse_meminfo(const std::string& text) {
    const auto total = field_kb(text, "MemTotal:");
    const auto available = field_kb(text, "MemAvailable:");
    if (!total || !available || *total <= 0) return std::nullopt;
    return Meminfo{*total, *available};
}

std::optional<CpuTimes> parse_cpu(const std::string& proc_stat) {
    std::istringstream in(proc_stat);
    std::string tag;
    in >> tag;
    if (tag != "cpu") return std::nullopt;
    // user nice system idle iowait irq softirq steal (guest time is already inside user)
    unsigned long long v[8] = {};
    for (auto& f : v)
        if (!(in >> f)) return std::nullopt;
    CpuTimes t;
    for (const auto f : v) t.total += f;
    t.busy = t.total - v[3] - v[4];  // idle + iowait = not working
    return t;
}

std::optional<int> cpu_percent(const CpuTimes& prev, const CpuTimes& cur) {
    if (cur.total <= prev.total || cur.busy < prev.busy) return std::nullopt;
    const auto dt = cur.total - prev.total;
    return static_cast<int>(std::lround(100.0 * static_cast<double>(cur.busy - prev.busy) / static_cast<double>(dt)));
}

NetTotals parse_netdev(const std::string& text) {
    NetTotals t;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;  // the two header lines
        auto name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(' '));
        if (name == "lo") continue;
        std::istringstream fields(line.substr(colon + 1));
        unsigned long long f[9] = {};
        for (auto& v : f)
            if (!(fields >> v)) break;
        t.rx += f[0];  // receive bytes
        t.tx += f[8];  // transmit bytes
    }
    return t;
}

std::optional<NetRate> net_rate(const NetTotals& prev, const NetTotals& cur, double dt_s) {
    if (dt_s <= 0 || cur.rx < prev.rx || cur.tx < prev.tx) return std::nullopt;
    return NetRate{
        static_cast<long>(std::llround(static_cast<double>(cur.rx - prev.rx) / dt_s)),
        static_cast<long>(std::llround(static_cast<double>(cur.tx - prev.tx) / dt_s)),
    };
}

std::optional<Throttled> parse_throttled(const std::string& out) {
    const std::string key = "throttled=0x";
    const auto pos = out.find(key);
    if (pos == std::string::npos) return std::nullopt;
    char* end = nullptr;
    const auto bits = std::strtoull(out.c_str() + pos + key.size(), &end, 16);
    if (end == out.c_str() + pos + key.size()) return std::nullopt;
    // https://www.raspberrypi.com/documentation/computers/os.html#get_throttled: bits 0-3 active, 16-19 latched
    static const char* names[] = {"undervoltage", "freq-capped", "throttled", "soft-temp-limit"};
    Throttled t;
    for (int i = 0; i < 4; ++i) {
        if (bits & (1ULL << i)) t.now.push_back(names[i]);
        if (bits & (1ULL << (i + 16))) t.ever.push_back(names[i]);
    }
    return t;
}

std::string sanitize_name(const std::string& name) {
    std::string out;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "host" : out;
}

json snapshot(const Sample& s) {
    json j{{"label", s.label}};
    json cpu{{"cores", s.cores}};
    if (s.cpu_pct) cpu["pct"] = *s.cpu_pct;
    if (s.mhz) cpu["mhz"] = *s.mhz;
    if (s.max_mhz) cpu["max_mhz"] = *s.max_mhz;
    j["cpu"] = std::move(cpu);
    j["mem"] = {{"used_mb", s.mem_used_kb / 1024}, {"total_mb", s.mem_total_kb / 1024}};
    const auto gb = [](std::uint64_t b) { return std::round(static_cast<double>(b) / 1e8) / 10.0; };
    j["disk"] = {{"used_gb", gb(s.disk_used_b)}, {"total_gb", gb(s.disk_total_b)}};
    if (s.temp_c) j["temp_c"] = std::round(*s.temp_c * 2) / 2;
    if (s.fan_rpm) j["fan_rpm"] = (*s.fan_rpm + 5) / 10 * 10;
    if (s.net) j["net"] = {{"rx_bps", (s.net->rx_bps + 512) / 1024 * 1024}, {"tx_bps", (s.net->tx_bps + 512) / 1024 * 1024}};
    if (s.throttled) j["throttled"] = {{"now", s.throttled->now}, {"ever", s.throttled->ever}};
    return j;
}

} // namespace buddy::system
