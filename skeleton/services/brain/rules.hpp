#pragma once
// Keyword/regex intent rules (English). Input is a raw whisper transcript ("Play, Spotify!");
// it is normalized first: lowercase, no punctuation, common whisper misspellings fixed,
// politeness ("please", "can you") stripped. Each rule must match the whole remaining text,
// so "pause" matches but "I don't want to pause anything right now" does not.

#include <regex>
#include <string>
#include <vector>

#include "intent.hpp"

namespace buddy::brain {

std::string normalize(std::string_view text);

class RuleParser : public IntentParser {
public:
    RuleParser();
    std::optional<Intent> parse(std::string_view text) const override;

private:
    struct Rule {
        std::regex pattern;
        std::string name;
        std::string skill;
        int query_group;  // capture group holding the query, 0 = none
    };
    std::vector<Rule> rules_;
};

} // namespace buddy::brain
