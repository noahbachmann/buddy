#include "state.hpp"

namespace buddy::web {

void StateStore::set(const std::string& topic, std::string payload) {
    {
        std::lock_guard lock(mutex_);
        auto& e = topics_[topic];
        if (e.version != 0 && e.payload == payload) return;  // retained resend after a reconnect
        e.payload = std::move(payload);
        e.version = ++version_;
    }
    changed_.notify_all();
}

std::vector<StateStore::Update> StateStore::wait(std::uint64_t& since, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, timeout, [&] { return closed_ || version_ > since; });
    std::vector<Update> out;
    if (closed_) return out;
    for (const auto& [topic, e] : topics_)
        if (e.version > since) out.push_back({topic, e.payload});
    since = version_;
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
