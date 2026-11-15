#include "command.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace buddy::messenger {

namespace {

using json = nlohmann::json;

[[noreturn]] void bad(const std::string& what) { throw std::invalid_argument(what); }

// "" when absent
std::string text_field(const json& msg, const char* key) {
    if (!msg.contains(key)) return "";
    if (!msg[key].is_string()) bad(std::string(key) + " is not a string");
    return trim(msg[key].get<std::string>());
}

std::string id_field(const json& msg) {
    std::string id = text_field(msg, "id");
    if (id.empty()) bad("id missing");
    if (id.size() > kMaxId) bad("id longer than " + std::to_string(kMaxId) + " bytes");
    return id;
}

std::string required_text(const json& msg) {
    std::string text = text_field(msg, "text");
    if (text.empty()) bad("text missing");
    return clip(text, kMaxText);
}

} // namespace

std::string trim(std::string_view s) {
    constexpr std::string_view space = " \t\r\n";
    const auto first = s.find_first_not_of(space);
    if (first == std::string_view::npos) return "";
    return std::string(s.substr(first, s.find_last_not_of(space) - first + 1));
}

std::string clip(std::string_view s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string(s);
    constexpr std::string_view ellipsis = "…";
    std::size_t end = max_bytes > ellipsis.size() ? max_bytes - ellipsis.size() : 0;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;  // not inside a character
    return std::string(s.substr(0, end)) + std::string(ellipsis);
}

Command parse_command(const std::string& payload) {
    const json msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    if (!msg.is_object()) bad("not a JSON object");
    const std::string action = text_field(msg, "action");

    if (action == "notify") {
        const std::string level = text_field(msg, "level");
        if (!level.empty() && level != "info" && level != "alert") bad("level is neither info nor alert");
        return Notify{.text = required_text(msg), .alert = level != "info", .key = clip(text_field(msg, "key"), kMaxId)};
    }
    if (action == "ask") {
        Ask ask{.id = id_field(msg), .text = required_text(msg), .options = {}, .from = clip(text_field(msg, "from"), 32),
                .ttl_s = std::nullopt};
        if (msg.contains("options")) {
            const json& options = msg["options"];
            if (!options.is_array()) bad("options is not a list");
            if (options.size() > kMaxOptions) bad("more than " + std::to_string(kMaxOptions) + " options");
            for (const auto& o : options) {
                if (!o.is_string() || trim(o.get<std::string>()).empty()) bad("an option is not a text");
                ask.options.push_back(clip(trim(o.get<std::string>()), kMaxOption));
            }
        }
        if (msg.contains("ttl_s")) {
            if (!msg["ttl_s"].is_number_integer() || msg["ttl_s"].get<std::int64_t>() < 0) bad("ttl_s is not a count of seconds");
            ask.ttl_s = msg["ttl_s"].get<std::int64_t>();
        }
        return ask;
    }
    if (action == "cancel") return Cancel{.id = id_field(msg)};
    bad(action.empty() ? "action missing" : "unknown action");
}

} // namespace buddy::messenger
