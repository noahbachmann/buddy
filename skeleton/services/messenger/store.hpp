// Small private JSON files: the bot's secrets (~/buddy/config/messenger.json) and the open questions
// (~/buddy/data/messenger/questions.json). Written whole, mode 0600, and swapped in atomically.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace buddy::messenger {

// null when the file doesn't exist. Throws std::runtime_error when it can't be read or isn't JSON.
nlohmann::json load_json(const std::string& path);

// Creates the directory. The old file stays intact until the new one is on the card (fsync, then rename).
// Throws std::runtime_error.
void save_json(const std::string& path, const nlohmann::json& content);

} // namespace buddy::messenger
