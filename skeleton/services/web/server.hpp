#pragma once
// HTTP routes of the panel (cpp-httplib), MQTT-free: commands go out through `publish`, state comes from the store.
//
//   GET  /                 static files from WebConfig::root (www/, installed to ~/buddy/share/web)
//   GET  /api/state        {"<topic>": <payload>, …} (payload: JSON, or a string like "online")
//   GET  /api/events       server-sent events, one per changed topic: {"topic", "payload"} (null = cleared), or
//                          {"topic", "patch"} (RFC 6902 against the payload this stream sent last; state.hpp);
//                          everything first, then changes; a comment every 15 s keeps proxies and phones awake
//   POST /api/cmd/<svc>    JSON body → buddy/<svc>/cmd (command topics only, access.hpp). 202, or 4xx + {"error"}

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "state.hpp"

namespace httplib {
class Server;
}

namespace buddy::web {

struct WebConfig {
    std::string root;                      // BUDDY_WEB_ROOT
    std::vector<std::string> extra_hosts;  // BUDDY_WEB_HOSTS
    std::chrono::milliseconds keepalive{15000};
};

using Publish = std::function<void(const std::string& topic, const std::string& payload)>;

// Adds routes and checks to `server`. Returns false if `root` isn't a directory.
bool add_routes(httplib::Server& server, StateStore& store, Publish publish, const WebConfig& cfg);

} // namespace buddy::web
