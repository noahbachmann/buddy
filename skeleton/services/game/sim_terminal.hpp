#pragma once
// `buddy-game --sim` without --games (#51): Dev plays Buddy in the terminal with real cards, through the same
// GameHost and payload the panel gets (5.6.4): the board is printed from buddy/game/state's payload, Dev answers with
// the payload's option index (`act`) or names the prompt's cards (`reveal`). Buddy's side runs on an in-process sim
// table (with --faults) and Buddy's AI answers at once. Nothing Dev sees comes from anywhere but the payload.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "game_host.hpp"
#include "riftbound/ai/player.hpp"
#include "sim.hpp"
#include "table.hpp"

namespace buddy::game {

struct TerminalOptions {
    std::string buddy_deck;  // a key of HostConfig::decks
    std::uint64_t seed = 1;
    table::SimFaults faults;
    std::unique_ptr<riftbound::ai::Player> buddy_ai;
};

class TerminalGame {
public:
    TerminalGame(const riftbound::CardDb& db, HostConfig cfg, TerminalOptions o);

    // Starts the game; returns what to print up to Dev's first input.
    std::string start();
    // One line of Dev's: a number (option or candidates), card codes, resume, concede, board, help. Returns what to
    // print: a refusal, or what happened up to Dev's next input.
    std::string input(const std::string& line);
    bool over() const;
    const GameHost& host() const { return host_; }

private:
    std::string pump();  // table commands and AI requests until Dev has to answer
    std::string send(const nlohmann::json& cmd);

    GameHost host_;
    TerminalOptions o_;
    table::Sim sim_;
    table::Table table_;
    std::vector<table::Command> pending_;
    std::vector<std::string> voice_;
};

// The payload as text: score, both sides, battlefields, chain, the last log lines, then Dev's options or prompt.
std::string render(const nlohmann::json& payload, int log_tail = 6);
// A prompt's answer as codes: candidate numbers or codes, separated by spaces or commas. Throws std::invalid_argument.
std::vector<std::string> prompt_answer(const nlohmann::json& payload, const std::string& line);

} // namespace buddy::game
