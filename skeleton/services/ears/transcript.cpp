#include "transcript.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace buddy::ears {

namespace {

bool iequal_prefix(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace

std::string drop_repeats(const std::string& text) {
    const auto end = text.find_first_of(".?!");
    if (end == std::string::npos || end == 0) return text;
    const std::string first = text.substr(0, end + 1);
    const std::string core = text.substr(0, end);  // without the punctuation
    const auto next = text.find_first_not_of(" .?!", end);
    if (next == std::string::npos) return text;
    const std::string rest = text.substr(next);
    // the whole first sentence again ("Play." + "Play something else." is no repeat), or a cut-off start of it
    // (max_tokens hit mid-sentence)
    const bool repeated =
        iequal_prefix(rest, core) && (rest.size() == core.size() || std::strchr(".?!", rest[core.size()]));
    if (repeated || iequal_prefix(core, rest)) return first;
    return text;
}

} // namespace buddy::ears
