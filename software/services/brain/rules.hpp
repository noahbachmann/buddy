#pragma once
// Keyword/regex intent rules (English). Input is a raw whisper transcript ("Play, Spotify!");
// it is normalized first: lowercase, no punctuation, number words as digits, common whisper misspellings fixed,
// politeness ("please", "can you") stripped. Each rule must match the whole remaining text,
// so "pause" matches but "I don't want to pause anything right now" does not.
// A trailing "on/in the <speaker>" is split off into Intent::device for the commands that start playback.
// Lights (skill hue) need a light word ("lights", "lamp", "scene", "brighter", "dim", "everything"), so music
// phrasings never reach them; the room or light name goes into Intent::device ("bedroom lights off", "… in the kitchen").
// Every phrasing in tools/voice/commands.tsv is checked by tests/test_corpus.cpp.

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
    std::optional<Intent> parse_core(const std::string& normalized) const;
    std::optional<Intent> parse_device(const std::string& normalized) const;
    std::optional<Intent> parse_room(const std::string& normalized) const;

    struct Rule {
        std::regex pattern;
        std::string name;
        std::string skill;
        int query_group;          // capture group holding the query, 0 = none
        std::string fixed_query;  // query when query_group is 0, e.g. "off" for "stop shuffling"
        int device_group = 0;     // capture group naming the lights ("the kitchen lights" → "kitchen"), 0 = none
    };
    std::vector<Rule> rules_;
};

} // namespace buddy::brain
