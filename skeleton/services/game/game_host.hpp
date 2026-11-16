#pragma once
// The game service's logic without MQTT or threads (5.6, #50): Dev's commands from buddy/game/cmd, one Session at a
// time, a SimDev for bot games, and the panel's payload with its version `v`. The service (#52) feeds it commands,
// table events and the AI's answers, and sends what take() returns.
//
// Commands (JSON, `action` + fields; an unknown field or value is refused, nothing changes):
//   {"action":"new", "buddy":"vi"|"zed", "seed"?: n, "dev":"human"|"greedy"|"random"}   refused while a game runs
//   {"action":"act", "v": n, "i": n}                one of the payload's options, at that version
//   {"action":"reveal", "v": n, "cards":[code…]}    the payload's prompt, Dev's cards by printed code
//   {"action":"resume"}  {"action":"concede"}
// A bot Dev (greedy, random) is a SimDev with a true shuffled deck, answered inside the host at once.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "riftbound/card_db.hpp"
#include "riftbound/deck.hpp"
#include "session.hpp"
#include "sim_dev.hpp"

namespace buddy::game {

struct HostDeck {
    riftbound::Deck deck;
    std::string text;  // the decklist, for the replay
};

struct HostConfig {
    std::map<std::string, HostDeck, std::less<>> decks;  // "vi", "zed": what `new` accepts for `buddy`
    std::function<std::uint64_t()> random_seed;           // `new` without a seed; unset: std::random_device
};

// A command's answer: ok, or why not (Dev reads it on the panel).
struct Reply {
    bool ok = true;
    std::string message;
};

struct HostOutputs {
    std::vector<table::Command> table;
    std::optional<AiRequest> ai;
    std::vector<std::string> voice;
    std::vector<std::string> replay;  // lines of the current game's replay
    bool new_game = false;            // the replay lines start a new file
    std::optional<nlohmann::json> state;  // the payload, when it changed since the last take() (`v` + 1)
};

class GameHost {
public:
    GameHost(const riftbound::CardDb& db, HostConfig cfg);

    Reply command(const nlohmann::json& cmd);
    void on_table(const table::Event& e);
    void on_ai(std::uint64_t id, const riftbound::Action& a);
    HostOutputs take();

    // The payload as last taken: {v, status, …, catalogue}; before any game {v, status: "idle"}.
    const nlohmann::json& state() const { return state_; }
    std::int64_t v() const { return v_; }
    const Session* session() const { return session_.get(); }

private:
    Reply start(const nlohmann::json& cmd);
    void absorb();  // the session's outputs into out_
    void drive();   // a bot Dev answers until it's someone else's turn

    const riftbound::CardDb& db_;
    HostConfig cfg_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<SimDev> bot_;
    nlohmann::json catalogue_;
    nlohmann::json game_;   // the payload's game part as last published
    nlohmann::json state_;  // the full payload as last published
    std::int64_t v_ = 0;
    HostOutputs out_;
};

} // namespace buddy::game
