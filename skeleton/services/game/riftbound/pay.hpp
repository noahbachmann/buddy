#pragma once
// Paying Energy and Power (131, 357, 445) from the rune pool and the basic runes' two abilities (164.2): exhaust for
// [1], recycle for Power of the rune's domain.
//
// Payment rule (docs/plans/05-riftbound.md → "5.2 design"): the player picks *what* to play, the engine pays. The pool
// goes first (it empties anyway), then runes: recycle exhausted runes before ready ones, exhaust the runes about to be
// recycled before the others (one rune gives [1] and then its Power). The one choice left is the domain when runes of
// several domains could pay a Power symbol: a Recycle decision with one rune per domain.

#include <vector>

#include "state.hpp"

namespace buddy::riftbound {

// Power is owed in `domains`; 0 = any domain ("rainbow").
struct Cost {
    int energy = 0;
    int power = 0;
    Domains domains = 0;
    bool operator==(const Cost&) const = default;
};

// 131 as printed. ⚠️ cards.json has only the number of Power symbols: they count as the card's domains (any domain
// for a colorless card). A card whose symbols say otherwise sets its cost in its rules (5.3).
Cost printed_cost(const Card& card);

// Runes `p` controls in their base: what pays (runes elsewhere get recalled, 323.6).
std::vector<ObjId> runes_of(const GameState& s, PlayerId p);

bool can_pay(const GameState& s, PlayerId p, const Cost& cost);

// Power the pool can't cover: how many runes get recycled.
int runes_to_recycle(const GameState& s, PlayerId p, const Cost& cost);

// What `p` may recycle next, given `picked`: one rune per domain (the one the engine would take: exhausted first, then
// base order), domains in non-decreasing order (one path per set), only picks that leave the rest payable. Empty
// once enough are picked.
std::vector<ObjId> recycle_options(const GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& picked);

// Pays `cost`: pool, then exhausts ready runes, then recycles `recycle` (a complete set from recycle_options).
// Throws std::logic_error if that doesn't add up.
void pay(GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& recycle);

} // namespace buddy::riftbound
