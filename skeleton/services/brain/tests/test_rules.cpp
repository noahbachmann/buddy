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

TEST(RuleParser, SkipTheSong) {  // eval 2026-10-28: whisper heard "Skip this song." as "Skip the song."
    const buddy::brain::RuleParser parser;
    for (const char* text : {"Skip the song.", "Skip that track.", "Skip the one."})
        EXPECT_EQ(parser.parse(text)->name, "next") << text;
}

TEST(RuleParser, SpokenNumbersInSpeakerNames) {  // whisper writes "desktop one" as often as "desktop 1"
    const buddy::brain::RuleParser parser;
    EXPECT_EQ(parser.parse("Play my Sleepy playlist on desktop one.")->device, "desktop 1");
    EXPECT_EQ(parser.parse("Switch to desktop one.")->device, "desktop 1");
    EXPECT_EQ(parser.parse("Play Kanye West on my Pixel eight.")->device, "pixel 8");
    EXPECT_EQ(parser.parse("Skip this one.")->name, "next");
}

TEST(RuleParser, WholeSentenceMustMatch) {
    const buddy::brain::RuleParser parser;
    EXPECT_EQ(parser.parse("Pause.")->name, "pause");
    EXPECT_FALSE(parser.parse("I don't want to pause anything right now"));
    EXPECT_FALSE(parser.parse(""));
}

TEST(RuleParser, LightsNeedALightWord) {  // music phrasings stay spotify
    const buddy::brain::RuleParser parser;
    EXPECT_EQ(parser.parse("Turn it down.")->skill, "spotify");
    EXPECT_EQ(parser.parse("Turn up the volume.")->skill, "spotify");
    EXPECT_EQ(parser.parse("Play City Lights by James Gruntz.")->skill, "spotify");
    EXPECT_EQ(parser.parse("Play Lights On.")->skill, "spotify");
    EXPECT_EQ(parser.parse("Play the movie scene.")->skill, "spotify");
    EXPECT_EQ(parser.parse("Turn the lights down.")->name, "darker");  // not colour "down"
    EXPECT_EQ(parser.parse("Turn the lights on in my bedroom.")->device, "bedroom");
}
