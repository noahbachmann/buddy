#include "transcript.hpp"

#include <cctype>
#include <vector>

namespace buddy::ears {

namespace {

struct Word {
    std::string lower;
    std::size_t end;  // one past the last character in the original text
};

// Letters, digits and apostrophes; everything else separates ("little.Turn" is two words).
std::vector<Word> words(const std::string& text) {
    std::vector<Word> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto word_char = [&](std::size_t j) {
            return std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '\'';
        };
        if (!word_char(i)) {
            ++i;
            continue;
        }
        Word w;
        for (; i < text.size() && word_char(i); ++i)
            w.lower += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        w.end = i;
        out.push_back(std::move(w));
    }
    return out;
}

} // namespace

std::string drop_repeats(const std::string& text) {
    const auto w = words(text);
    // shortest period k: the whole text is the first k words repeated (the last copy may be cut off by max_tokens)
    for (std::size_t k = 1; 2 * k <= w.size(); ++k) {
        bool periodic = true;
        for (std::size_t i = k; i < w.size() && periodic; ++i) periodic = w[i].lower == w[i - k].lower;
        if (!periodic) continue;
        std::size_t end = w[k - 1].end;
        if (end < text.size() && (text[end] == '.' || text[end] == '?' || text[end] == '!')) ++end;  // keep "."
        return text.substr(0, end);
    }
    return text;
}

} // namespace buddy::ears
