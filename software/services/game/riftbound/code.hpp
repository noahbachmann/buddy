#pragma once
// Printing codes: which printing a physical card is. Printed at the bottom of the card face ("OGN • 007a/298",
// "UNL • 229*/219", runes "VEN • R01 • EN"); Riftcodex ids are the same in lower case ("ogn-007a-298", "ven-r01").
// Later the table's scanner reads them, so the parser takes every spelling of one code.

#include <optional>
#include <string>
#include <string_view>

namespace buddy::riftbound {

struct Code {
    std::string set;     // "OGN"
    std::string prefix;  // letters before the number: "R" (VEN runes), "T" (tokens), "SP"; mostly empty
    int number = 0;
    char variant = 0;    // 'a'/'b' alternate art, '*' signature; 0 = the plain printing
    int total = 0;       // "/298": cards in the set; 0 = not given. Not part of str(): a check, not an identity

    // Canonical, the key printings are stored under: "OGN-007a", "UNL-229*", "VEN-R01" (prefix + number = 3 chars).
    std::string str() const;
};

// "OGN-007a/298", "ogn • 7a", "OGN 007A", "ogn-007a-298", "VEN • R01 • EN", "UNL-229*". Separators: '-', '/', blanks,
// '•' and '·'; case doesn't matter; a trailing language ("EN") is ignored. nullopt = not a code.
std::optional<Code> parse_code(std::string_view text);

} // namespace buddy::riftbound
