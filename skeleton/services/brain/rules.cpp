#include "rules.hpp"

#include <cctype>
#include <map>
#include <sstream>

namespace buddy::brain {

namespace {

// Applied to the normalized text, in order.
const std::vector<std::pair<std::regex, std::string>>& rewrites() {
    static const std::vector<std::pair<std::regex, std::string>> r = {
        // whisper spellings of "Spotify"
        {std::regex(R"(\bspot+(y|i)? ?(f(y|i|ie|ai))\b)"), "spotify"},
        {std::regex(R"(\bspot (to|the|a|of) ?(find|fine|fly|five)\b)"), "spotify"},  // seen 2026-10-23: "Play spot to find"
        // politeness and addressing, at either end
        {std::regex(R"(^(hey |ok |okay )?(buddy |jarvis )?(please |can you |could you |would you |will you )*)"), ""},
        {std::regex(R"(( please| for me| buddy| thanks| thank you| right now| now)+$)"), ""},
        // whisper sometimes hears "play" as past tense or third person
        {std::regex(R"(^(played|plays|playing) )"), "play "},
        // ... and "my" as "mode", "ma", "me", … in "play my playlist"
        {std::regex(R"(^play (from )?(mode|ma|mai|me|mi|mine|mike) (playlist|list)\b)"), "play $1my $3"},
        // "put on X" = "play X"
        {std::regex(R"(^put on )"), "play "},
        {std::regex(R"( percent\b)"), ""},
    };
    return r;
}

// "fifty" → "50", "twenty five" → "25" (whisper writes most numbers as digits, but not always)
std::string number_words_to_digits(const std::string& text) {
    static const std::map<std::string, int> units = {
        {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
        {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13}, {"fourteen", 14},
        {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}};
    static const std::map<std::string, int> tens = {{"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fifty", 50},
                                                    {"sixty", 60}, {"seventy", 70}, {"eighty", 80}, {"ninety", 90}};
    std::istringstream in(text);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::string w = words[i];
        if (const auto t = tens.find(w); t != tens.end()) {
            int n = t->second;
            if (i + 1 < words.size()) {
                if (const auto u = units.find(words[i + 1]); u != units.end() && u->second < 10) {
                    n += u->second;
                    ++i;
                }
            }
            w = std::to_string(n);
        } else if (w == "hundred" || (w == "a" && i + 1 < words.size() && words[i + 1] == "hundred")) {
            if (w == "a") ++i;
            w = "100";
        } else if (const auto u = units.find(w); u != units.end() && u->second >= 10) {
            w = std::to_string(u->second);  // "one", "two" stay words: "skip this one", "bound two"
        }
        out += (out.empty() ? "" : " ") + w;
    }
    return out;
}

// "play music" / "play spotify" carry no query: resume instead of searching.
bool generic_query(const std::string& q) {
    static const std::regex generic(R"((some |my |the )?(music|spotify|something|songs?|tunes)( on spotify)?)");
    return std::regex_match(q, generic);
}

// Filler that can't be part of a name: "songs by taylor swift" → "taylor swift", "the song party by daughter" →
// "party by daughter", "bound 2 from kanye west" → "bound 2 by kanye west" (spotify searches "<title> by
// <artist>" with field filters). "some strokes" stays: "Some Nights" is a song; spotify ignores the "some".
std::string canonical_query(std::string q) {
    static const std::regex filler(R"(^((songs|music|something|anything|stuff|tracks) by|the song|the track|a song by) )");
    q = std::regex_replace(q, filler, "");
    if (const auto from = q.rfind(" from "); from != std::string::npos && from > 0) q.replace(from, 6, " by ");
    return q;
}

} // namespace

std::string normalize(std::string_view text) {
    std::string out;
    bool space = true;  // drops leading spaces and collapses runs
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            out += static_cast<char>(std::tolower(u));
            space = false;
        } else if (c != '\'' && !space) {  // "don't" → "dont"
            out += ' ';
            space = true;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    out = number_words_to_digits(out);
    for (const auto& [pattern, replacement] : rewrites()) out = std::regex_replace(out, pattern, replacement);
    return out;
}

RuleParser::RuleParser() {
    auto add = [this](const char* pattern, const char* name, int query_group = 0, const char* fixed_query = "") {
        rules_.push_back({std::regex(pattern), name, "spotify", query_group, fixed_query});
    };
    const char* amount = R"(( (a )?(little|bit|lot)( bit)?)?)";  // "turn it down a little bit"

    // Order matters: "play the next song" is "next", not a search for "the next song".
    add(R"((pause|stop)( the)?( music| song| spotify| playback| it| playing| this)?)", "pause");
    add(R"((stop shuffling|shuffle off|turn off shuffle|turn shuffle off|no shuffle))", "shuffle", 0, "off");
    add(R"((shuffle( on| the music| it| this)?|turn on shuffle|turn shuffle on))", "shuffle", 0, "on");
    add(R"((stop repeating|repeat off|turn off repeat|turn repeat off|no repeat))", "repeat", 0, "off");
    add(R"((repeat( this| the)?( song| track)|(play|put) (this|the) (song|track) on repeat))", "repeat", 0, "track");
    add(R"((repeat( on)?|turn on repeat))", "repeat", 0, "context");
    add(R"((whats|what is) (playing|this song|this|the song|that song|that)|(what|which) song is (this|that|playing))",
        "now_playing");
    add(R"(((play|go to) )?(the )?(next|skip)( (song|track|it|one|this( song| track| one)?))?)", "next");
    add(R"(((play|go to) )?(the )?(previous|last) (song|track|one)|previous|go back( one( song| track)?)?|back)",
        "previous");
    add(R"((set |turn )?(the )?(volume|it|the music) (to |at )?([0-9]{1,3}))", "set_volume", 5);
    add((std::string(R"((turn (it|the music|the volume|this) up|turn up (the )?(music|volume|it)|)") +
         R"(((make it|a|a little|a bit|a little bit) )?louder|volume up))" + amount)
            .c_str(),
        "volume_up");
    add((std::string(R"((turn (it|the music|the volume|this) down|turn down (the )?(music|volume|it)|)") +
         R"(((make it|a|a little|a bit|a little bit) )?(quieter|softer)|volume down))" + amount)
            .c_str(),
        "volume_down");
    add(R"(shuffle (my )?(liked|favorite|favourite|saved) (songs|tracks|music))", "play_liked", 0, "shuffle");
    add(R"(play (my )?(liked|favorite|favourite|saved) (songs|tracks|music))", "play_liked");
    add(R"((resume|continue|unpause|keep playing)( (the )?(music|spotify|playing|playback|song))?)", "play");
    // Own playlists: "play my playlist chill vibes", "play my chill vibes playlist", "play the focus playlist"
    add(R"(play (from )?my (playlist|list)( on spotify)?)", "play_playlist");
    add(R"(play (from )?my (playlist|list) (.+?)( on spotify)?)", "play_playlist", 3);
    add(R"(play (my|the) (.+?) (playlist|list)( on spotify)?)", "play_playlist", 2);
    add(R"(play (.+?)( on spotify)?)", "play", 1);
    add(R"(play|music)", "play");
}

std::optional<Intent> RuleParser::parse(std::string_view text) const {
    const std::string t = normalize(text);
    std::smatch m;
    for (const auto& rule : rules_) {
        if (!std::regex_match(t, m, rule.pattern)) continue;
        Intent intent{rule.name, rule.skill, rule.fixed_query};
        if (rule.query_group > 0) {
            std::string q = m[rule.query_group].str();
            if (rule.name == "play") q = canonical_query(q);
            if (!generic_query(q)) intent.query = q;
        }
        return intent;
    }
    return std::nullopt;
}

} // namespace buddy::brain
