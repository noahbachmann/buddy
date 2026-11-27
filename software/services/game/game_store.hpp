#pragma once
// The service's game files (#52): per game a journal (GameHost's inputs, what a restart resumes from) and the referee
// replay (`buddy-game --replay` reads it), appended as the game goes, in `dir` (~/buddy/data/riftbound/games):
//   <yyyy-mm-dd-hhmmss>-<seed>.journal   one JSON line per input
//   <yyyy-mm-dd-hhmmss>-<seed>.txt       the replay
// The journal is written before anything is sent (a restart resends what's open; the table answers a repeat once),
// the replay after it; a line cut off by a crash was never acted on and is dropped when read.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game_host.hpp"

namespace buddy::game {

struct SavedGame {
    std::filesystem::path journal, replay;
    std::vector<std::string> lines;  // the journal's complete lines
    std::string replay_text;
};

class GameStore {
public:
    explicit GameStore(std::filesystem::path dir);  // creates it

    // A new game's files; `stamp` = its start time (yyyy-mm-dd-hhmmss), a name taken gets "-2", "-3", ….
    void begin(std::string_view stamp, std::uint64_t seed);
    // After every take(): the journal's lines, then the replay's. Nothing open: dropped (no game). `replay_text`
    // (after an undo, #97) replaces the replay file first.
    void append(const std::vector<std::string>& journal, const std::vector<std::string>& replay,
                const std::optional<std::string>& replay_text = std::nullopt);

    // The newest game's files, if any.
    std::optional<SavedGame> latest() const;
    // Goes on with a saved game: its replay rewritten as the restored host has it, appends after that.
    void resume(const SavedGame& g, const std::string& replay_text);
    // A game that can't be restored: renamed to *.journal.broken (kept for a look, never resumed).
    static void set_aside(const SavedGame& g);

    const std::filesystem::path& dir() const { return dir_; }
    const std::filesystem::path& journal_path() const { return journal_path_; }

private:
    std::filesystem::path dir_, journal_path_, replay_path_;
    std::ofstream journal_, replay_;
};

// The local time as a file stamp: 2026-11-16-213005.
std::string file_stamp();

struct Resumed {
    std::unique_ptr<GameHost> host;  // null: nothing to resume
    HostOutputs open;                // GameHost::restore's: what to send again
    std::string note;                // what happened, for the log
};
// The service's start: the newest game, if unfinished, restored into a new host, its files reopened for appending.
// A journal that doesn't restore, or whose replay file isn't a prefix of the restored replay (another build, a
// damaged file), is set aside.
Resumed resume_latest(GameStore& store, const riftbound::CardDb& db, const HostConfig& cfg);

} // namespace buddy::game
