#pragma once
// Last payload per topic (retained buddy/+/state and buddy/+/status), with a version counter so each browser
// connection gets only what changed since it last looked. Written by Paho's thread, read by the HTTP threads.
//
// Each change is encoded once into ready server-sent-event frames that every connection shares:
//   data: {"topic": …, "payload": …}     whole payload (JSON, or a string like "online"; null = cleared)
//   data: {"topic": …, "patch": […]}     RFC 6902 JSON Patch against the previous payload, when smaller

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace buddy::web {

using Frame = std::shared_ptr<const std::string>;

class StateStore {
public:
    void set(const std::string& topic, const std::string& payload);

    // Frames for the topics changed after version `since` (0 = everything): a patch where the caller already has
    // the version it applies to, else the whole payload. `since` becomes the newest version. Waits up to `timeout`
    // when nothing changed. Returns at once after close().
    std::vector<Frame> wait(std::uint64_t& since, std::chrono::milliseconds timeout);

    // {"<topic>": payload, …} (null = cleared)
    nlohmann::json snapshot() const;

    // Wakes all waiters for good (shutdown).
    void close();
    bool closed() const;

private:
    struct Entry {
        std::string raw;        // as received: unchanged resends are dropped
        nlohmann::json value;   // parsed; null = cleared
        Frame full;
        Frame patch;            // null: none, or not smaller than `full`
        std::uint64_t version = 0;
        std::uint64_t patch_base = 0;  // version `patch` applies to
    };
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, Entry> topics_;
    std::uint64_t version_ = 0;
    bool closed_ = false;
};

} // namespace buddy::web
