#pragma once
// A simulated Dev (2.6.3, #49) for tests, the soak and bot games: Dev's real cards as a true shuffled deck the
// referee never sees. It answers the session's prompts truthfully and plays with an ai::Player on the true state
// (the referee's state with Dev's true cards in place of the placeholders), answering with one of dev_options().
//
// The truth is per object. Dev's hidden hand cards are interchangeable for the referee: when the session's option
// names another hidden hand object than the one SimDev's player picked (or declares the spell a cost discards),
// SimDev swaps the two objects' true cards, as a real Dev would just play the card in their hand.

#include <cstdint>
#include <memory>
#include <vector>

#include "riftbound/ai/player.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/state.hpp"
#include "session.hpp"

namespace buddy::game {

class SimDev {
public:
    // `shuffle` false: the placeholders are the truth (the scripted Dev of the #48 tests).
    SimDev(PlayerId seat, std::unique_ptr<riftbound::ai::Player> player, std::uint64_t seed, bool shuffle = true);

    // Right after Session::start(): `s` = its state. Deals the true deck, runes and battlefield.
    void begin(const riftbound::GameState& s, const riftbound::Deck& seat0, const riftbound::Deck& seat1);
    // Dev's decision: one of `options` (Session::dev_options(), state `s`). Throws std::logic_error if the move
    // SimDev's player picks on the true state isn't among them (a gap in the options).
    DevOption choose(const riftbound::GameState& s, const std::vector<DevOption>& options);
    // The true cards of a prompt.
    std::vector<const riftbound::Card*> reveal(const DevPrompt& p) const;

    // `s` with Dev's true cards on every object Buddy doesn't know.
    riftbound::GameState true_state(const riftbound::GameState& s) const;
    // Dev's true card of `obj` (objects created after begin(): nullptr).
    const riftbound::Card* truth(ObjId obj) const { return obj < truth_.size() ? truth_[obj] : nullptr; }
    PlayerId seat() const { return seat_; }

private:
    bool hidden_hand(const riftbound::GameState& s, ObjId obj) const;
    const DevOption* declared(const riftbound::GameState& t, const riftbound::Action& a,
                              const std::vector<DevOption>& options);

    PlayerId seat_;
    std::unique_ptr<riftbound::ai::Player> player_;
    riftbound::Rng rng_;
    bool shuffle_;
    std::vector<const riftbound::Card*> truth_;  // by ObjId; Buddy's objects nullptr
};

} // namespace buddy::game
