#include "throttle.hpp"

namespace buddy::hue {

Throttle::Clock::duration Throttle::interval(const std::string& type) {
    using std::chrono::milliseconds;
    if (type == "light") return milliseconds(100);
    return milliseconds(1000);  // grouped_light: a Zigbee broadcast to the whole group; scene: many lights
}

void Throttle::push(Request r) {
    const std::string key = r.type + "/" + r.id;
    if (const auto it = pending_.find(key); it != pending_.end()) {
        it->second.body.merge_patch(r.body);
        return;
    }
    pending_.emplace(key, std::move(r));
}

std::vector<Request> Throttle::due(Clock::time_point now) {
    std::vector<Request> out;
    for (auto it = pending_.begin(); it != pending_.end();) {
        const auto last = last_sent_.find(it->first);
        if (last != last_sent_.end() && now < last->second + interval(it->second.type)) {
            ++it;
            continue;
        }
        last_sent_[it->first] = now;
        out.push_back(std::move(it->second));
        it = pending_.erase(it);
    }
    return out;
}

std::optional<Throttle::Clock::time_point> Throttle::next() const {
    std::optional<Clock::time_point> next;
    for (const auto& [key, r] : pending_) {
        const auto last = last_sent_.find(key);
        const auto at = last == last_sent_.end() ? Clock::time_point{} : last->second + interval(r.type);
        if (!next || at < *next) next = at;
    }
    return next;
}

} // namespace buddy::hue
