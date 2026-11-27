#pragma once
// Matching names as whisper hears them against names in an app (Spotify speakers and playlists, Hue rooms and
// lights): case and punctuation don't count, a word may be slightly misspelled, dropped or added.

#include <cstddef>
#include <string>
#include <vector>

namespace buddy::names {

std::string lower(std::string s);

// Letters and digits only: "AC/DC" == "ac dc", "Guns N' Roses" == "guns n roses"
std::string squash(const std::string& s);

// Levenshtein distance: letters to insert, delete or change to turn a into b.
std::size_t edit_distance(const std::string& a, const std::string& b);

// How well a name matches what was said: exact (ignoring case/punctuation) 1000 > contains it 500 >
// per spoken word: 2 if it's in the name, 1 if a name word is close to it (whisper: "type" for "hype").
// 0 = no match.
int match_score(const std::string& name, const std::string& spoken);

// Spoken name made comparable: lower case, no leading "the"/"my", "one"…"nine" as digits ("my pixel eight" = "pixel 8").
std::string spoken_name(const std::string& spoken);

// Indices of the names that match `spoken` best. Accepted: the name equals or contains what was said, or every
// spoken word is (close to) a word of the name ("bathroom speakers"). Empty = none; more than one = ambiguous.
std::vector<std::size_t> best_matches(const std::vector<std::string>& names, const std::string& spoken);

} // namespace buddy::names
