#pragma once
// Host cards for /status: the last buddy/system*/state per host (D23) condensed into one short line each, so
// "what's the status" in the chat shows every Pi at a glance. No I/O; main.cpp feeds it from the bus.

#include <string>
#include <vector>

namespace buddy::messenger {

class Fleet {
public:
    // service = the topic's middle part ("system", "system-pi-2"), payload = its retained state JSON.
    // Anything that isn't a system service or doesn't parse is ignored.
    void state(const std::string& service, const std::string& payload);

    // From buddy/<service>/status: an offline host shows "offline" instead of stale numbers.
    void status(const std::string& service, bool online);

    // One line per host, newline-separated, sorted by label; "" until a host has reported.
    std::string summary() const;

private:
    struct Host {
        std::string service;
        std::string label;
        std::string line;  // everything after the marker and label
        bool online = true;
    };
    std::vector<Host> hosts_;
};

} // namespace buddy::messenger
