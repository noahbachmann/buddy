#include "rules.hpp"

#include <cctype>

namespace buddy::brain {

namespace {

// Applied to the normalized text, in order.
const std::vector<std::pair<std::regex, std::string>>& rewrites() {
    static const std::vector<std::pair<std::regex, std::string>> r = {
        // whisper spellings of "Spotify"
        {std::regex(R"(\bspot+(y|i)? ?(f(y|i|ie|ai))\b)"), "spotify"},
        // politeness and addressing, at either end
        {std::regex(R"(^(hey |ok |okay )?(buddy |jarvis )?(please |can you |could you |would you |will you )*)"), ""},
        {std::regex(R"(( please| for me| buddy| thanks| thank you)+$)"), ""},
        // whisper sometimes hears "play" as past tense or third person
        {std::regex(R"(^(played|plays|playing) )"), "play "},
        // ... and "my" as "mode", "ma", "me", … in "play my playlist"
        {std::regex(R"(^play (from )?(mode|ma|mai|me|mi|mine|mike) (playlist|list)\b)"), "play $1my $3"},
    };
    return r;
}

// "play music" / "play spotify" carry no query: resume instead of searching.
bool generic_query(const std::string& q) {
    static const std::regex generic(
        R"((some |my |the )?(music|spotify|something|songs?|tunes)( on spotify)?)");
    return std::regex_match(q, generic);
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
    for (const auto& [pattern, replacement] : rewrites()) out = std::regex_replace(out, pattern, replacement);
    return out;
}

RuleParser::RuleParser() {
    auto add = [this](const char* pattern, const char* name, int query_group = 0) {
        rules_.push_back({std::regex(pattern), name, "spotify", query_group});
    };
    // Order matters: "play the next song" is "next", not a search for "the next song".
    add(R"((pause|stop)( the)?( music| song| spotify| playback| it| playing)?)", "pause");
    add(R"(((play|go to) )?(the )?(next|skip)( (song|track|one|this( song| track)?))?)", "next");
    add(R"(((play|go to) )?(the )?(previous|last) (song|track|one)|previous|go back|back)", "previous");
    add(R"((turn (it|the music|the volume|this) up|turn up (the )?(music|volume)|(make it )?louder|volume up))",
        "volume_up");
    add(R"((turn (it|the music|the volume|this) down|turn down (the )?(music|volume)|(make it )?(quieter|softer)|volume down))",
        "volume_down");
    add(R"((resume|continue|unpause)( (the )?(music|spotify|playing|playback|song))?)", "play");
    // Own playlists: "play my playlist chill vibes", "play my chill vibes playlist", "play my playlist"
    add(R"(play (from )?my (playlist|list)( on spotify)?)", "play_playlist");
    add(R"(play (from )?my (playlist|list) (.+?)( on spotify)?)", "play_playlist", 3);
    add(R"(play my (.+?) (playlist|list)( on spotify)?)", "play_playlist", 1);
    add(R"(play (.+?)( on spotify)?)", "play", 1);
    add(R"(play|music)", "play");
}

std::optional<Intent> RuleParser::parse(std::string_view text) const {
    const std::string t = normalize(text);
    std::smatch m;
    for (const auto& rule : rules_) {
        if (!std::regex_match(t, m, rule.pattern)) continue;
        Intent intent{rule.name, rule.skill, ""};
        if (rule.query_group > 0) {
            const std::string q = m[rule.query_group].str();
            if (!generic_query(q)) intent.query = q;
        }
        return intent;
    }
    return std::nullopt;
}

} // namespace buddy::brain
