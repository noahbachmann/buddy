#include "state.hpp"

namespace buddy::web {

namespace {

using json = nlohmann::json;

json payload_json(const std::string& payload) {
    if (payload.empty()) return nullptr;
    auto j = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    return j.is_discarded() ? json(payload) : j;
}

Frame frame(const json& event) { return std::make_shared<const std::string>("data: " + event.dump() + "\n\n"); }

bool structured(const json& j) { return j.is_object() || j.is_array(); }

} // namespace

void StateStore::set(const std::string& topic, const std::string& payload) {
    json value = payload_json(payload);  // outside the lock: HTTP threads keep streaming meanwhile
    {
        std::lock_guard lock(mutex_);
        auto& e = topics_[topic];
        if (e.version != 0 && e.raw == payload) return;  // retained resend after a reconnect
        Frame full = frame({{"topic", topic}, {"payload", value}});
        Frame patch;
        if (e.version != 0 && structured(e.value) && structured(value)) {
            patch = frame({{"topic", topic}, {"patch", json::diff(e.value, value)}});
            if (patch->size() >= full->size()) patch = nullptr;
        }
        e.raw = payload;
        e.value = std::move(value);
        e.full = std::move(full);
        e.patch = std::move(patch);
        e.patch_base = e.version;
        e.version = ++version_;
    }
    changed_.notify_all();
}

std::vector<Frame> StateStore::wait(std::uint64_t& since, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, timeout, [&] { return closed_ || version_ > since; });
    std::vector<Frame> out;
    if (closed_) return out;
    for (const auto& [topic, e] : topics_) {
        if (e.version <= since) continue;
        // since >= patch_base: the caller got everything up to `since`, and this topic didn't change between
        // patch_base and version, so it holds the payload the patch starts from.
        out.push_back(e.patch && since >= e.patch_base ? e.patch : e.full);
    }
    since = version_;
    return out;
}

nlohmann::json StateStore::snapshot() const {
    std::lock_guard lock(mutex_);
    json out = json::object();
    for (const auto& [topic, e] : topics_) out[topic] = e.value;
    return out;
}

void StateStore::close() {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    changed_.notify_all();
}

bool StateStore::closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
}

} // namespace buddy::web
