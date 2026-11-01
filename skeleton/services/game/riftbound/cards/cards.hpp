#pragma once
// The decks' cards (5.3, D21): one CardRules per card name, written from the card text (Riftcodex home printing;
// Might Bonus and effect text of Equipment from the printed card, plan → "Equipment as printed"). CardDb::parse
// attaches them, so every loaded card with an entry here is implemented. Basic runes need none (engine).

#include <span>
#include <string_view>

namespace buddy::riftbound {
class CardRules;
}

namespace buddy::riftbound::cards {

struct Entry {
    std::string_view name;  // as decks know it: "Vi, Hotheaded"
    const CardRules* rules;
};

std::span<const Entry> vi_unleashed();  // vi_unleashed.cpp
std::span<const Entry> zed_vendetta();  // zed_vendetta.cpp

// The rules registered under `name`, nullptr if none.
const CardRules* rules_for(std::string_view name);

} // namespace buddy::riftbound::cards
