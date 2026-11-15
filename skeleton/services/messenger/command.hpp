// buddy/messenger/cmd payloads → typed commands, validated and clipped to what a chat message holds.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace buddy::messenger {

// Bytes. Telegram takes 4096 characters per message: a question plus its answer line has to fit.
inline constexpr std::size_t kMaxText = 3500;
inline constexpr std::size_t kMaxOption = 60;  // a button label
inline constexpr std::size_t kMaxOptions = 8;
inline constexpr std::size_t kMaxId = 64;

struct Notify {
    std::string text;
    bool alert = true;  // false: arrives without sound
    std::string key;    // same key again: see Chat
};

struct Ask {
    std::string id;  // the asker's; comes back in every event
    std::string text;
    std::vector<std::string> options;      // buttons; empty = free text
    std::string from;                      // asking service, shown in front of the question
    std::optional<std::int64_t> ttl_s;     // nullopt = the configured default, 0 = never expires
};

struct Cancel {
    std::string id;
};

using Command = std::variant<Notify, Ask, Cancel>;

// Throws std::invalid_argument saying what is wrong. Never quotes the payload: message texts stay out of the journal.
Command parse_command(const std::string& payload);

// At most max_bytes, cut between UTF-8 characters, ending in "…" when cut.
std::string clip(std::string_view s, std::size_t max_bytes);

std::string trim(std::string_view s);

} // namespace buddy::messenger
