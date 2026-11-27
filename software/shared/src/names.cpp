#include "buddy/names.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace buddy::names {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string squash(const std::string& s) {
    std::string out;
    for (const unsigned char c : s)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

std::size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

int match_score(const std::string& name, const std::string& spoken) {
    if (squash(name) == squash(spoken)) return 1000;
    if (!squash(spoken).empty() && squash(name).find(squash(spoken)) != std::string::npos) return 500;
    auto words = [](const std::string& s) {
        std::vector<std::string> out;
        std::string w;
        for (const unsigned char c : s + " ") {
            if (std::isalnum(c)) w += static_cast<char>(std::tolower(c));
            else if (!w.empty()) out.push_back(std::exchange(w, ""));
        }
        return out;
    };
    const auto in_name = words(name);
    int score = 0;
    for (const auto& w : words(spoken)) {
        if (std::find(in_name.begin(), in_name.end(), w) != in_name.end()) {
            score += 2;
            continue;
        }
        const std::size_t allowed = w.size() >= 7 ? 2 : w.size() >= 3 ? 1 : 0;  // "a"/"my" must match exactly
        for (const auto& n : in_name) {
            if (allowed > 0 && edit_distance(w, n) <= allowed) {
                score += 1;
                break;
            }
        }
    }
    return score;
}

std::string spoken_name(const std::string& spoken) {
    static const std::vector<std::string> digits = {"zero", "one", "two", "three", "four",
                                                    "five", "six", "seven", "eight", "nine"};
    std::istringstream in(lower(spoken));
    std::string out;
    for (std::string w; in >> w;) {
        if (out.empty() && (w == "the" || w == "my")) continue;
        if (const auto d = std::find(digits.begin(), digits.end(), w); d != digits.end())
            w = std::to_string(d - digits.begin());
        out += (out.empty() ? "" : " ") + w;
    }
    return out;
}

std::vector<std::size_t> best_matches(const std::vector<std::string>& names, const std::string& spoken) {
    const std::string want = spoken_name(spoken);
    const int words = static_cast<int>(std::count(want.begin(), want.end(), ' ')) + 1;
    const int min_score = std::max(2, words);  // one exact word, or each word at least close
    int best_score = 0;
    std::vector<std::size_t> best;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const int score = match_score(names[i], want);
        if (score < min_score || score < best_score) continue;
        if (score > best_score) best.clear();
        best_score = score;
        best.push_back(i);
    }
    return best;
}

} // namespace buddy::names
