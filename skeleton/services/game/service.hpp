#pragma once
// buddy-game without flags: the game service (5.6, #52). One GameHost behind MQTT: Dev's commands from
// buddy/game/cmd, the table on buddy/table/{cmd,event}, Buddy's AI (ISMCTS) on a worker thread, the payload retained
// on buddy/game/state, voice lines to buddy/voice/say, the game files in BUDDY_GAME_GAMES (a restart resumes the
// unfinished game from its journal).

#include <filesystem>

#include "riftbound/card_db.hpp"

namespace buddy::game {

// Runs until SIGTERM. Throws on bad config before MQTT starts.
int run_service(const riftbound::CardDb& db, const std::filesystem::path& decks_dir);

} // namespace buddy::game
