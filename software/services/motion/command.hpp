#pragma once
// buddy/motion/cmd payloads → Command. Checks shape only; joint and gesture names are checked where they're used.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace buddy::motion {

struct Command {
    std::string action;                   // move | center | relax | gesture
    std::map<std::string, float> joints{};  // move: targets; center: which joints (values ignored), empty = all
    std::optional<int> ms{};                // move
    std::string query{};                    // gesture name
};

// Error text for bad JSON, a missing or unknown action, or wrongly typed fields.
std::variant<Command, std::string> parse_command(std::string_view payload);

} // namespace buddy::motion
