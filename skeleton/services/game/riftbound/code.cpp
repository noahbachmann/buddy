#include "code.hpp"

#include <cctype>

namespace buddy::riftbound {

namespace {

bool is_alpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// Upper case ASCII; '•' (E2 80 A2) and '·' (C2 B7) become blanks.
std::string simplify(std::string_view in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in.substr(i, 3) == "\xE2\x80\xA2") {
            out += ' ';
            i += 2;
        } else if (in.substr(i, 2) == "\xC2\xB7") {
            out += ' ';
            i += 1;
        } else {
            out += upper(in[i]);
        }
    }
    return out;
}

// Reads while pred, up to max chars; returns what it read.
template <class Pred>
std::string take(const std::string& s, std::size_t& i, std::size_t max, Pred pred) {
    const std::size_t start = i;
    while (i < s.size() && i - start < max && pred(s[i])) ++i;
    return s.substr(start, i - start);
}

std::size_t skip_separators(const std::string& s, std::size_t& i, bool allow_slash) {
    const std::size_t start = i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '-' || (allow_slash && s[i] == '/'))) ++i;
    return i - start;
}

} // namespace

std::string Code::str() const {
    std::string digits = std::to_string(number);
    const std::size_t width = prefix.size() < 3 ? 3 - prefix.size() : 1;
    if (digits.size() < width) digits.insert(0, width - digits.size(), '0');
    std::string s = set + "-" + prefix + digits;
    if (variant != 0) s += variant;
    return s;
}

std::optional<Code> parse_code(std::string_view text) {
    const std::string s = simplify(text);
    std::size_t i = 0;
    skip_separators(s, i, false);

    Code code;
    code.set = take(s, i, 4, is_alpha);
    if (code.set.size() < 2 || skip_separators(s, i, false) == 0) return std::nullopt;

    code.prefix = take(s, i, 2, is_alpha);
    const std::string digits = take(s, i, 3, is_digit);
    if (digits.empty() || (i < s.size() && is_digit(s[i]))) return std::nullopt;
    code.number = std::stoi(digits);
    if (i < s.size() && (s[i] == '*' || (is_alpha(s[i]) && (i + 1 == s.size() || !is_alpha(s[i + 1]))))) {
        code.variant = s[i] == '*' ? '*' : lower(s[i]);
        ++i;
    }

    std::size_t gap = skip_separators(s, i, true);
    if (gap > 0 && i < s.size() && is_digit(s[i])) {
        const std::string total = take(s, i, 3, is_digit);
        if (i < s.size() && is_digit(s[i])) return std::nullopt;
        code.total = std::stoi(total);
        gap = skip_separators(s, i, false);
    }
    if (i < s.size() && is_alpha(s[i])) {
        if (gap == 0) return std::nullopt;  // "142AB": not a variant, not a language
        const std::string language = take(s, i, 2, is_alpha);
        if (language.size() != 2 || (i < s.size() && is_alpha(s[i]))) return std::nullopt;
        skip_separators(s, i, false);
    }
    if (i != s.size()) return std::nullopt;
    return code;
}

} // namespace buddy::riftbound
