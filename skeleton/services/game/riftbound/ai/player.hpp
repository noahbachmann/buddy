#pragma once
// AI players (5.4): what a player sees and answers. A player never gets the referee's GameState, only view(s, seat)
// and both decklists (Buddy knows Dev's precon list): a leak is impossible by construction. Design:
// docs/plans/05-riftbound.md → "5.4 design".

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

#include "riftbound/deck.hpp"
#include "riftbound/rng.hpp"
#include "riftbound/state.hpp"

namespace buddy::riftbound::ai {

class Player {
public:
    virtual ~Player() = default;
    virtual std::string name() const = 0;

    // A new game: this player's seat and both decklists (seat 0, seat 1 as in new_game).
    virtual void begin(PlayerId seat, const Deck& seat0, const Deck& seat1) = 0;
    // A decision of this player with two or more legal actions. `view` = view(s, seat), not playable as it is
    // (determinize() it); `actions` = legal_actions(s), the same in every determinization of the view (#27).
    // Returns one of `actions`.
    virtual Action choose(const GameState& view, const std::vector<Action>& actions) = 0;
};

// Uniform among the legal actions, its own Rng.
class RandomPlayer : public Player {
public:
    explicit RandomPlayer(std::uint64_t seed) : rng_(seed) {}
    std::string name() const override { return "random"; }
    void begin(PlayerId, const Deck&, const Deck&) override {}
    Action choose(const GameState&, const std::vector<Action>& actions) override {
        return actions.at(rng_.below(actions.size()));
    }

private:
    Rng rng_;
};

// One move independent of which object holds a card: hand and trash cards by card (two Zaps in hand = one move; an
// opponent's hidden hand card is the same move whichever id holds it in a determinization), everything else
// (board objects, battlefields, players) by id, plus kind and n. Search merges actions with equal keys.
struct ActionKey {
    ActionKind kind = ActionKind::Done;
    const Card* card = nullptr;  // hand or trash card; nullptr = by id
    ObjId obj = no_obj;
    std::uint8_t n = 0;
    auto operator<=>(const ActionKey&) const = default;
};
ActionKey key_of(const GameState& s, const Action& a);

// `actions` without the later ones whose key an earlier one has, order kept.
std::vector<Action> distinct(const GameState& s, const std::vector<Action>& actions);

} // namespace buddy::riftbound::ai
