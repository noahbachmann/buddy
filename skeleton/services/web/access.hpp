#pragma once
// Who may use the panel, and what it may send. No login (LAN only, D19); instead:
//  - Host must be an IP, localhost or a *.local name (or listed in BUDDY_WEB_HOSTS): a web page elsewhere can't
//    reach the panel through a DNS name of its own that points at the Pi (DNS rebinding).
//  - Commands need Content-Type application/json and, if the browser sends one, an Origin equal to the Host:
//    another site open in the same browser can't send a "simple" cross-site POST.
//  - Only buddy/<service>/cmd topics in topics.json (not planned) can be published.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace buddy::web {

bool allowed_host(std::string_view host, const std::vector<std::string>& extra_hosts);
bool same_origin(std::string_view origin, std::string_view host);

// "hue" → "buddy/hue/cmd" if that is a command topic in use, else nullopt.
std::optional<std::string> command_topic(std::string_view service);

// Empty = a valid command ({"action": "<non-empty string>", …}); else why not.
std::string command_error(std::string_view body);

} // namespace buddy::web
