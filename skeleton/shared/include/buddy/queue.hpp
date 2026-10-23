#pragma once
// Hand-off from Paho's callback thread to a service's main thread, so slow work (HTTP, speech
// synthesis) never blocks MQTT.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace buddy {

template <typename T>
class BlockingQueue {
public:
    void push(T item) {
        {
            std::lock_guard lock(mutex_);
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    // nullopt after `timeout` without an item: lets the caller re-check running().
    std::optional<T> pop(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [&] { return !items_.empty(); })) return std::nullopt;
        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> items_;
};

} // namespace buddy
