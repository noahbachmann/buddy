#pragma once
// Last payload per topic (retained buddy/+/state and buddy/+/status), with a version counter so each browser
// connection gets only what changed since it last looked. Written by Paho's thread, read by the HTTP threads.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace buddy::web {

class StateStore {
public:
    struct Update {
        std::string topic;
        std::string payload;  // empty = the retained message was cleared
    };

    void set(const std::string& topic, std::string payload);

    // Topics changed after version `since` (0 = everything), latest payload each; `since` becomes the newest
    // version. Waits up to `timeout` when nothing changed. Returns at once after close().
    std::vector<Update> wait(std::uint64_t& since, std::chrono::milliseconds timeout);

    // Wakes all waiters for good (shutdown).
    void close();
    bool closed() const;

private:
    struct Entry {
        std::string payload;
        std::uint64_t version = 0;
    };
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, Entry> topics_;
    std::uint64_t version_ = 0;
    bool closed_ = false;
};

} // namespace buddy::web
