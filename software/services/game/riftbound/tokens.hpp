#pragma once
// The tokens the decks' cards create (179–187). A token's characteristics come from the effect that makes it (187),
// so they are defined here, not in cards.json: same text wherever the decks create them. Play one with
// actions.hpp play_token().

#include "card_db.hpp"

namespace buddy::riftbound::tokens {

// "a 1 [M] Bird unit token with [Deflect]" (Carrion Dredger, Trapping Grounds)
const Card& bird();
// "a 0 [M] Shadow Clone unit token" with "When I attack, you may banish a unit from your trash. If you do, give me
// [Assault 4] this turn." (Zed, From the Shadows; Zed, Without a Sound; Death Mark)
const Card& shadow_clone();
// "Gold gear token": "[Reaction][>] Kill this, [E]: [Add] [A]." (Yeti Brawler)
const Card& gold();

} // namespace buddy::riftbound::tokens
