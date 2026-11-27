#include "command.hpp"

#include <nlohmann/json.hpp>

namespace buddy::motion {

std::variant<Command, std::string> parse_command(std::string_view payload) {
    using json = nlohmann::json;
    const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    if (msg.is_discarded() || !msg.is_object()) return std::string("not a JSON object");
    if (!msg.contains("action") || !msg["action"].is_string()) return std::string("\"action\" missing");

    Command c{.action = msg["action"]};
    if (c.action != "move" && c.action != "center" && c.action != "relax" && c.action != "gesture")
        return "unknown action " + c.action;
    if (msg.contains("joints")) {
        if (!msg["joints"].is_object()) return std::string("\"joints\" must be an object");
        for (const auto& [name, deg] : msg["joints"].items()) {
            if (!deg.is_number() && c.action != "center") return "angle of " + name + " must be a number";
            c.joints[name] = deg.is_number() ? deg.get<float>() : 0.0f;
        }
    }
    if (msg.contains("ms")) {
        if (!msg["ms"].is_number_integer() || msg["ms"].get<long>() < 0) return std::string("\"ms\" must be ≥ 0");
        c.ms = static_cast<int>(std::min<long>(msg["ms"].get<long>(), 100000));
    }
    if (msg.contains("query")) {
        if (!msg["query"].is_string()) return std::string("\"query\" must be a string");
        c.query = msg["query"];
    }
    if (c.action == "move" && c.joints.empty()) return std::string("move without joints");
    if (c.action == "gesture" && c.query.empty()) return std::string("gesture without query");
    return c;
}

} // namespace buddy::motion
