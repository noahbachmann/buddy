#include "random_game.hpp"

#include <algorithm>
#include <bit>
#include <exception>

#include "engine.hpp"

namespace buddy::riftbound {

namespace {

// Checks beyond check_invariants() at the turn player's first open priority of a turn's Main phase (Beginning-phase
// triggers may have used a chain before): Awaken readied everything (315.1), the pools emptied (316.3).
std::string check_turn_start(const GameState& s) {
    const PlayerId p = s.turn_player;
    for (const Object& o : s.objects) {
        if (on_board(o.zone) && o.controller == p && o.exhausted) return o.card->name + " still exhausted after Awaken";
    }
    for (const Player& pl : s.players) {
        if (pl.pool != Pool{}) return "rune pool not empty in the main phase";
    }
    return {};
}

int scored_count(const GameState& s) {
    int n = 0;
    for (const Player& pl : s.players) n += std::popcount(pl.scored);
    return n;
}

int triggered_on_chain(const GameState& s) {
    return static_cast<int>(
        std::count_if(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return c.kind == ItemKind::Triggered; }));
}


long card_count(const GameState& s) {
    return std::count_if(s.objects.begin(), s.objects.end(), [](const Object& o) { return !o.card->token; });
}

} // namespace

GameChecks::GameChecks(const GameState& start)
    : cards_(card_count(start)), checked_turn_(start.phase >= Phase::Main ? start.turn : 0) {}  // mid-turn: done

std::string GameChecks::check(const GameState& s) {
    auto problems = check_invariants(s);
    if (card_count(s) != cards_) problems.push_back("card count changed");
    if (s.decision.kind == DecisionKind::Priority && s.phase == Phase::Main && s.chain.empty() &&
        s.turn != checked_turn_) {
        checked_turn_ = s.turn;
        if (auto p = check_turn_start(s); !p.empty()) problems.push_back(p);
    }
    return problems.empty() ? std::string{} : problems.front();
}

RandomGame play_random(const Deck& seat0, const Deck& seat1, std::uint64_t seed, int max_actions, bool vanilla) {
    RandomGame result;
    std::string last = "setup";
    try {
        GameState s = new_game(seat0, seat1, {.seed = seed, .vanilla = vanilla});
        Rng pick(seed ^ 0x5eedc0ffee);  // the players' own stream, apart from the game's
        GameChecks checks(s);
        if (auto p = checks.check(s); !p.empty()) throw std::logic_error(p);
        while (!s.winner) {
            if (result.actions == max_actions) throw std::logic_error("no winner yet");
            const auto actions = legal_actions(s);
            const Action a = actions.at(pick.below(actions.size()));
            last = describe(s, a);
            result.log.push_back(a);
            if (a.kind == ActionKind::Play) {
                result.spells += s.obj(a.obj).card->type == CardType::Spell;
                result.reactions += closed(s);
            }
            result.activations += a.kind == ActionKind::Activate;
            result.choices += s.decision.kind == DecisionKind::Choose;
            result.moves += a.kind == ActionKind::Move;
            const int before = triggered_on_chain(s);
            const int scored = scored_count(s);
            const bool showdown = s.showdown.has_value(), combat = showdown && s.showdown->combat;
            apply(s, a);
            result.triggers += std::max(0, triggered_on_chain(s) - before);
            // lower bounds: an EndTurn resets the masks before the next Hold, a showdown that ends and the next
            // one that begins within one apply count once
            result.scorings += std::max(0, scored_count(s) - scored);
            result.showdowns += !showdown && s.showdown.has_value();
            result.combats += !combat && s.showdown && s.showdown->combat;
            ++result.actions;
            result.turns = s.turn;
            if (auto p = checks.check(s); !p.empty()) throw std::logic_error(p);
        }
        result.winner = s.winner;
        for (PlayerId p = 0; p < player_count; ++p) result.points[p] = s.player(p).points;
    } catch (const std::exception& e) {
        result.problem = "turn " + std::to_string(result.turns) + ", action " + std::to_string(result.actions) +
                         " (" + last + "): " + e.what();
    }
    return result;
}

} // namespace buddy::riftbound
