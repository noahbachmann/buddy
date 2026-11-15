#include "watch.hpp"

#include <algorithm>
#include <utility>

namespace buddy::messenger {

std::string duration_text(std::chrono::seconds d) {
    const auto s = d.count();
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min";
    return std::to_string(s / 3600) + " h" + (s % 3600 >= 60 ? " " + std::to_string(s % 3600 / 60) + " min" : "");
}

Watch::Watch(const std::vector<std::string>& services, std::chrono::seconds offline_after, Clock::time_point now)
    : offline_after_(offline_after) {
    for (const auto& name : services) {
        Service s;
        s.name = name;
        s.since = now;
        services_.push_back(std::move(s));
    }
}

void Watch::status(const std::string& service, bool online, Clock::time_point now) {
    const auto it = std::ranges::find(services_, service, &Service::name);
    if (it == services_.end() || it->online == online) return;
    if (online) it->was_off = now - it->since;
    else it->drops.push_back(now);
    it->online = online;
    it->since = now;
}

std::vector<Notify> Watch::step(Clock::time_point now) {
    std::vector<Notify> out;
    if (offline_after_.count() <= 0) return out;
    for (auto& s : services_) {
        const std::string key = "watch:" + s.name;
        while (!s.drops.empty() && now - s.drops.front() >= kFlapWindow) s.drops.pop_front();

        if (!s.online && !s.alerted && now - s.since >= offline_after_) {
            s.alerted = true;
            out.push_back({.text = "⚠ " + s.name + " is offline", .alert = true, .key = key});
        } else if (s.online && s.alerted) {
            s.alerted = false;
            const auto off = std::chrono::duration_cast<std::chrono::seconds>(s.was_off);
            out.push_back({.text = "✓ " + s.name + " is back (was off " + duration_text(off) + ")", .alert = false, .key = key});
        }

        if (!s.flapping && s.drops.size() >= kFlapCount) {
            s.flapping = true;
            out.push_back({.text = "⚠ " + s.name + " keeps restarting", .alert = true, .key = key});
        } else if (s.flapping && s.online && s.drops.empty()) {
            s.flapping = false;
            out.push_back({.text = "✓ " + s.name + " runs steadily again", .alert = false, .key = key});
        }
    }
    return out;
}

std::optional<Watch::Clock::time_point> Watch::deadline() const {
    std::optional<Clock::time_point> first;
    auto consider = [&](Clock::time_point t) { first = first ? std::min(*first, t) : t; };
    if (offline_after_.count() <= 0) return first;
    for (const auto& s : services_) {
        if (!s.online && !s.alerted) consider(s.since + offline_after_);
        if (s.flapping && s.online && !s.drops.empty()) consider(s.drops.back() + kFlapWindow);
    }
    return first;
}

std::string Watch::summary() const {
    std::string offline;
    std::size_t online = 0;
    for (const auto& s : services_) {
        if (s.online) ++online;
        else offline += (offline.empty() ? "" : ", ") + s.name;
    }
    if (offline.empty()) return "All " + std::to_string(online) + " services online.";
    return "Offline: " + offline + ". Online: " + std::to_string(online) + ".";
}

} // namespace buddy::messenger
