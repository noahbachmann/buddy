#pragma once
// The panel's view of a game (2.6, #50): the payload of buddy/game/state. Built from public_view() of the referee's
// state only, so it can't carry Buddy's hand or Dev's placeholders: everything here is either known to both players
// or Dev's own decision (options, prompt) computed so that it doesn't depend on the placeholders either. Players are
// "buddy" and "dev", never seats. Layout: docs/02-riftbound/design.md → "2.6 as built" (#50).

#include <nlohmann/json.hpp>

#include "riftbound/deck.hpp"
#include "session.hpp"

namespace buddy::game {

// The game part of the payload (no `v`, no catalogue): board, status, Dev's options or prompt, log.
nlohmann::json public_state(const Session& session);
// The same for another arrangement of the hidden cards (the leak test): `s` instead of session.state(), `options`
// instead of session.dev_options(); everything else from the session.
nlohmann::json public_state(const Session& session, const riftbound::GameState& s, const std::vector<DevOption>& options);

// Every card of both decks plus the tokens, by code (code_of: home printing, "token:Bird"): name, type, cost, power,
// might, domains, tags, text (Riftcodex markup as is).
nlohmann::json catalogue(const riftbound::Deck& seat0, const riftbound::Deck& seat1);

} // namespace buddy::game
