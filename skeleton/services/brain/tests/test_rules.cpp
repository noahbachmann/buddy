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

TEST(RuleParser, CommandWordWithATail) {  // eval 2026-10-28: "Next try", "Go back once off", "Bibbles louder"
    const buddy::brain::RuleParser parser;
    for (const auto& [text, name] : std::vector<std::pair<const char*, const char*>>{
             {"Next tune.", "next"},
             {"Next stroke on Spotify.", "next"},
             {"Go back a song.", "previous"},
             {"Go back one more.", "previous"},
             {"Pause the radio.", "pause"},
             {"Much louder.", "volume_up"},
             {"Make it louder.", "volume_up"},
             {"A lot louder.", "volume_up"},
             {"Even quieter.", "volume_down"},
             {"Turn it down a notch.", "volume_down"},
             {"Turn it up a little bit.", "volume_up"},
         })
        EXPECT_EQ(parser.parse(text)->name, name) << text;
    // a number is a level, "to" a speaker, a longer sentence something else
    EXPECT_EQ(parser.parse("Turn the volume to 30.")->name, "set_volume");
    EXPECT_FALSE(parser.parse("Turn it down to 30."));
    EXPECT_FALSE(parser.parse("Go back to the Pixel."));
    EXPECT_FALSE(parser.parse("Next to Normal."));
    EXPECT_EQ(parser.parse("Play the next episode.")->name, "play");
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

TEST(RuleParser, DemoGesture) {  // live 2026-10-29: whisper's versions of "Andi's"; the corpus has the canonical one
    const buddy::brain::RuleParser parser;
    for (const char* text : {"Hey Jarvis, cut off Andi's balls.", "Cut off Andy's balls!", "cut of andis balls",
                             "Cut off on these balls.", "Cut off undies balls.", "Cut off undistballs."}) {
        const auto intent = parser.parse(text);
        ASSERT_TRUE(intent) << text;
        EXPECT_EQ(intent->skill, "motion") << text;
        EXPECT_EQ(intent->query, "shake") << text;
    }
    EXPECT_FALSE(parser.parse("Cut off the music."));
    EXPECT_FALSE(parser.parse("Cut off all of the old balls."));  // 4 words: something else
}
