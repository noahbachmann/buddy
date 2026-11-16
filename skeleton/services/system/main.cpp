// system: host metrics for the panel (1.17). Reads /proc, /sys and vcgencmd every BUDDY_SYSTEM_INTERVAL_S and
// publishes a retained snapshot — only when the rounded values changed, so an idle host stays silent. One instance
// per host: BUDDY_SYSTEM_NAME unset = service "system" (this Pi), set ("Pi 2") = service "system-pi-2" with its own
// client id, status and state topic, all matching the web panel's buddy/+/state as-is.
//
//   buddy-system            live: buddy/<service>/state out
//   buddy-system --once     test: print one snapshot (no MQTT)
//
// Config (env, see deploy/buddy.env.example): BUDDY_SYSTEM_*
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <sys/statvfs.h>
#include <unistd.h>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/systemd.hpp"
#include "metrics.hpp"

namespace {

using namespace buddy;
using namespace buddy::system;
namespace fs = std::filesystem;

struct Config {
    std::string name = env_string("BUDDY_SYSTEM_NAME", "");  // card label; set on extra hosts ("Pi 2")
    // watchdog (60 s) needs a ping from the loop: keep samples at most 15 s apart
    int interval_s = std::clamp(env_int("BUDDY_SYSTEM_INTERVAL_S", 5), 1, 15);
};

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::optional<long> read_long(const std::string& path) {
    const auto text = read_file(path);
    if (!text) return std::nullopt;
    try {
        return std::stol(*text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The Active Cooler's tach: the hwmon named pwmfan (absent without a fan).
std::optional<std::string> find_fan() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/hwmon", ec)) {
        const auto name = read_file(e.path() / "name");
        if (name && name->find("pwmfan") != std::string::npos) return e.path() / "fan1_input";
    }
    return std::nullopt;
}

// Collects one Sample; paths that don't exist on a host simply leave their field out.
class Collector {
public:
    explicit Collector(std::string label) : label_(std::move(label)), fan_path_(find_fan()) {
        max_mhz_ = read_long("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    }

    Sample sample() {
        Sample s;
        s.label = label_;
        s.cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
        if (const auto text = read_file("/proc/stat"))
            if (const auto cur = parse_cpu(*text)) {
                if (prev_cpu_) s.cpu_pct = cpu_percent(*prev_cpu_, *cur);
                prev_cpu_ = cur;
            }
        if (const auto khz = read_long("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"))
            s.mhz = static_cast<int>(*khz / 1000);
        if (max_mhz_) s.max_mhz = static_cast<int>(*max_mhz_ / 1000);
        if (const auto text = read_file("/proc/meminfo"))
            if (const auto m = parse_meminfo(*text)) {
                s.mem_total_kb = m->total_kb;
                s.mem_used_kb = m->total_kb - m->available_kb;
            }
        struct statvfs fsinfo{};
        if (statvfs("/", &fsinfo) == 0) {
            s.disk_total_b = static_cast<std::uint64_t>(fsinfo.f_blocks) * fsinfo.f_frsize;
            s.disk_used_b = static_cast<std::uint64_t>(fsinfo.f_blocks - fsinfo.f_bavail) * fsinfo.f_frsize;
        }
        if (const auto milli = read_long("/sys/class/thermal/thermal_zone0/temp"))
            s.temp_c = static_cast<double>(*milli) / 1000.0;
        if (fan_path_)
            if (const auto rpm = read_long(*fan_path_)) s.fan_rpm = static_cast<int>(*rpm);
        const auto now = std::chrono::steady_clock::now();
        if (const auto text = read_file("/proc/net/dev")) {
            const auto cur = parse_netdev(*text);
            if (prev_net_)
                s.net = net_rate(*prev_net_, cur, std::chrono::duration<double>(now - prev_net_at_).count());
            prev_net_ = cur;
            prev_net_at_ = now;
        }
        s.throttled = throttle_flags();
        return s;
    }

private:
    // vcgencmd talks to the firmware (works for dev: status.sh uses it); one warning, then off for good.
    std::optional<Throttled> throttle_flags() {
        if (!vcgencmd_) return std::nullopt;
        std::string out;
        if (FILE* p = popen("vcgencmd get_throttled 2>/dev/null", "r")) {
            char buf[128];
            while (fgets(buf, sizeof buf, p)) out += buf;
            if (pclose(p) != 0) out.clear();
        }
        auto flags = parse_throttled(out);
        if (!flags) {
            std::cout << "[system] vcgencmd get_throttled unavailable: no throttle flags" << std::endl;
            vcgencmd_ = false;
        }
        return flags;
    }

    std::string label_;
    std::optional<std::string> fan_path_;
    std::optional<long> max_mhz_;
    std::optional<CpuTimes> prev_cpu_;
    std::optional<NetTotals> prev_net_;
    std::chrono::steady_clock::time_point prev_net_at_{};
    bool vcgencmd_ = true;
};

std::string hostname() {
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) != 0) return "host";
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();
    const bool once = argc == 2 && std::string(argv[1]) == "--once";
    if (argc != 1 && !once) {
        std::cerr << "usage: " << argv[0] << " [--once]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[system] " << e.what() << std::endl;
        return 1;
    }

    const std::string label = cfg->name.empty() ? hostname() : cfg->name;
    const std::string service = cfg->name.empty() ? "system" : "system-" + sanitize_name(cfg->name);
    Collector collector(label);

    collector.sample();  // prime the CPU and network counters: the first published snapshot has rates
    if (once) {
        sleep(std::chrono::milliseconds(500));
        std::cout << snapshot(collector.sample()).dump(2) << std::endl;
        return 0;
    }

    Mqtt mqtt(service, mqtt_config_from_env());
    if (!mqtt.start()) return 0;
    const std::string topic = "buddy/" + service + "/state";
    std::cout << "[system] " << label << " → " << topic << " every " << cfg->interval_s << " s" << std::endl;

    std::string published;
    while (running()) {
        systemd::notify_watchdog();
        if (auto payload = snapshot(collector.sample()).dump(); payload != published) {
            mqtt.publish(topic, payload, 0, true);
            published = std::move(payload);
        }
        sleep(std::chrono::seconds(cfg->interval_s));
    }

    mqtt.stop();
    return 0;
}
