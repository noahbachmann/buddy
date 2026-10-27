// normalize() and the whole-match rule on phrasings the corpus doesn't contain.
#include <gtest/gtest.h>

#include "rules.hpp"

using buddy::brain::normalize;

TEST(Normalize, PunctuationAndCase) {
    EXPECT_EQ(normalize("  Play,   Spotify!  "), "play spotify");
    EXPECT_EQ(normalize("Don't stop"), "dont stop");
}

TEST(Normalize, NumberWords) {
    EXPECT_EQ(normalize("volume twenty five"), "volume 25");
    EXPECT_EQ(normalize("volume a hundred"), "volume 100");
    EXPECT_EQ(normalize("skip this one"), "skip this one");  // "one", "two" stay words
}

TEST(Normalize, PolitenessAndWhisperSpellings) {
    EXPECT_EQ(normalize("Hey buddy, can you play jazz please"), "play jazz");
    EXPECT_EQ(normalize("Play spot to find"), "play spotify");
}

TEST(RuleParser, WholeSentenceMustMatch) {
    const buddy::brain::RuleParser parser;
    EXPECT_EQ(parser.parse("Pause.")->name, "pause");
    EXPECT_FALSE(parser.parse("I don't want to pause anything right now"));
    EXPECT_FALSE(parser.parse(""));
}
