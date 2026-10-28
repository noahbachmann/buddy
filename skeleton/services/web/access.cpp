#include "access.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "buddy/topics.hpp"

namespace buddy::web {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// "buddy.local:8080" → "buddy.local", "[fe80::1]:8080" → "fe80::1"
std::string host_name(std::string_view host) {
    if (host.starts_with('[')) {
        const auto end = host.find(']');
        return end == std::string_view::npos ? std::string(host) : std::string(host.substr(1, end - 1));
    }
    return std::string(host.substr(0, host.find(':')));
}

bool ipv4(std::string_view s) {
    int parts = 0, digits = 0;
    for (const char c : s) {
        if (c == '.') {
            if (digits == 0) return false;
            ++parts;
            digits = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c)) && ++digits <= 3) {
            continue;
        } else {
            return false;
        }
    }
    return parts == 3 && digits > 0;
}

} // namespace

bool allowed_host(std::string_view host, const std::vector<std::string>& extra_hosts) {
    const std::string name = lower(host_name(host));
    if (name.empty()) return false;
    if (name == "localhost" || ipv4(name) || name.find(':') != std::string::npos) return true;  // ':' = IPv6 literal
    if (name.ends_with(".local") && name.size() > 6) return true;
    return std::any_of(extra_hosts.begin(), extra_hosts.end(), [&](const std::string& h) { return lower(h) == name; });
}

bool same_origin(std::string_view origin, std::string_view host) {
    for (const std::string_view scheme : {"http://", "https://"})
        if (origin.starts_with(scheme)) return lower(origin.substr(scheme.size())) == lower(host);
    return false;  // "null" (file://, sandboxed frames) and anything odd
}

std::optional<std::string> command_topic(std::string_view service) {
    const std::string topic = "buddy/" + std::string(service) + "/cmd";
    for (const auto t : topic::commands)
        if (t == topic) return topic;
    return std::nullopt;
}

std::string command_error(std::string_view body) {
    const auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return "not JSON";
    if (!j.is_object()) return "not a JSON object";
    if (!j.contains("action") || !j["action"].is_string() || j["action"].get<std::string>().empty())
        return "\"action\" missing";
    return "";
}

} // namespace buddy::web
