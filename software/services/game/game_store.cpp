#include "game_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace buddy::game {

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// Orders the files: the stamp, then a clash's number ("<stamp>-<seed>-2" after "<stamp>-<seed>").
std::pair<std::string, int> order(const fs::path& p) {
    const std::string name = p.stem().string();
    const auto stamp = std::min<std::size_t>(17, name.size());  // yyyy-mm-dd-hhmmss
    const auto dash = name.find('-', stamp + 1);
    int n = 1;
    if (dash != std::string::npos) n = std::atoi(name.c_str() + dash + 1);
    return {name.substr(0, stamp), n};
}

void write_lines(std::ofstream& out, const std::vector<std::string>& lines) {
    for (const auto& l : lines) out << l << '\n';
}

} // namespace

GameStore::GameStore(fs::path dir) : dir_(std::move(dir)) { fs::create_directories(dir_); }

void GameStore::begin(std::string_view stamp, std::uint64_t seed) {
    journal_.close();
    replay_.close();
    const std::string base = std::string(stamp) + "-" + std::to_string(seed);
    std::string name = base;
    for (int n = 2; fs::exists(dir_ / (name + ".journal")) || fs::exists(dir_ / (name + ".txt")); ++n) {
        name = base + "-" + std::to_string(n);
    }
    journal_path_ = dir_ / (name + ".journal");
    journal_.open(journal_path_, std::ios::binary);
    replay_path_ = dir_ / (name + ".txt");
    replay_.open(replay_path_, std::ios::binary);
    if (!journal_ || !replay_) throw std::runtime_error("can't write " + journal_path_.string());
}

void GameStore::append(const std::vector<std::string>& journal, const std::vector<std::string>& replay,
                       const std::optional<std::string>& replay_text) {
    if (!journal_.is_open()) return;
    if (!journal.empty()) {
        write_lines(journal_, journal);
        journal_.flush();
    }
    if (replay_text) {  // journal first: a crash in between restores the undo and rewrites the replay at start
        replay_.close();
        std::ofstream(replay_path_, std::ios::binary | std::ios::trunc) << *replay_text;
        replay_.open(replay_path_, std::ios::binary | std::ios::app);
    }
    if (!replay.empty()) {
        write_lines(replay_, replay);
        replay_.flush();
    }
    if (!journal_ || !replay_) throw std::runtime_error("can't write " + journal_path_.string());
}

std::optional<SavedGame> GameStore::latest() const {
    std::optional<fs::path> newest;
    for (const auto& e : fs::directory_iterator(dir_)) {
        if (e.path().extension() != ".journal") continue;
        if (!newest || order(e.path()) > order(*newest)) newest = e.path();
    }
    if (!newest) return std::nullopt;
    SavedGame g;
    g.journal = *newest;
    g.replay = fs::path(*newest).replace_extension(".txt");
    const std::string text = read(g.journal);
    std::size_t at = 0;
    for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) {
        g.lines.push_back(text.substr(at, end - at));
    }  // a last line without its newline: cut off by a crash, never acted on
    g.replay_text = read(g.replay);
    return g;
}

void GameStore::resume(const SavedGame& g, const std::string& replay_text) {
    journal_.close();
    replay_.close();
    journal_path_ = g.journal;
    replay_path_ = g.replay;
    {  // drop a cut-off last line before appending after it
        std::string text;
        for (const auto& l : g.lines) text += l + '\n';
        std::ofstream(g.journal, std::ios::binary | std::ios::trunc) << text;
    }
    std::ofstream(g.replay, std::ios::binary | std::ios::trunc) << replay_text;
    journal_.open(g.journal, std::ios::binary | std::ios::app);
    replay_.open(g.replay, std::ios::binary | std::ios::app);
    if (!journal_ || !replay_) throw std::runtime_error("can't write " + journal_path_.string());
}

void GameStore::set_aside(const SavedGame& g) {
    fs::rename(g.journal, fs::path(g.journal).concat(".broken"));
}

Resumed resume_latest(GameStore& store, const riftbound::CardDb& db, const HostConfig& cfg) {
    Resumed r;
    const auto saved = store.latest();
    if (!saved) return r;
    const std::string name = saved->journal.filename().string();
    auto host = std::make_unique<GameHost>(db, cfg);
    try {
        r.open = host->restore(saved->lines);
    } catch (const JournalError& e) {
        GameStore::set_aside(*saved);
        r.note = name + " set aside: " + e.what();
        return r;
    }
    const Session* s = host->session();
    if (s == nullptr || s->status() == Status::Over) {
        r.note = name + ": the last game is over";
        return r;
    }
    const std::string replay = s->replay_text();
    if (!replay.starts_with(saved->replay_text)) {  // written after the journal: never ahead of it
        GameStore::set_aside(*saved);
        r.note = name + " set aside: its replay isn't the restored game's";
        return r;
    }
    store.resume(*saved, replay);
    r.note = name + " resumed: " + std::to_string(saved->lines.size()) + " inputs, " +
             std::string(to_string(s->status()));
    r.host = std::move(host);
    return r;
}

std::string file_stamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d-%H%M%S", &local);
    return buf;
}

} // namespace buddy::game
