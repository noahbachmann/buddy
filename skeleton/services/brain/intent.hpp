#pragma once
// Text → intent. The rule parser is the first implementation; an LLM-backed parser can replace it later
// without touching the routing in main.cpp.

#include <optional>
#include <string>
#include <string_view>

namespace buddy::brain {

struct Intent {
    std::string name;   // e.g. "play", "play_playlist", "pause", "next", "previous", "volume_up", "volume_down"
    std::string skill;  // service that handles it, e.g. "spotify"
    std::string query;  // optional argument, e.g. "daft punk" in "play daft punk", "chill vibes" in "play my chill vibes playlist"
};

class IntentParser {
public:
    virtual ~IntentParser() = default;
    // nullopt = not understood
    virtual std::optional<Intent> parse(std::string_view text) const = 0;
};

} // namespace buddy::brain
