#include <gtest/gtest.h>

#include "transcript.hpp"

using buddy::ears::drop_repeats;

TEST(Transcript, DropsRepetitionLoops) {  // eval 2026-10-28, installed model with ctx=auto
    EXPECT_EQ(drop_repeats("Turn it down a little.Turn it down a little.Turn it down a little."),
              "Turn it down a little.");
    EXPECT_EQ(drop_repeats("Play Bound 2 from Kanye West.Play Bound 2 from Kanye West.Play Bound 2"),
              "Play Bound 2 from Kanye West.");
    EXPECT_EQ(drop_repeats("Pause. pause."), "Pause.");
    // stock model, no punctuation, cut off by max_tokens (Pi, 2026-10-28)
    EXPECT_EQ(drop_repeats("skip the song skip the song skip the song skip"), "skip the song");
}

TEST(Transcript, KeepsEverythingElse) {
    EXPECT_EQ(drop_repeats("Play Daft Punk."), "Play Daft Punk.");
    EXPECT_EQ(drop_repeats("Play Daft Punk"), "Play Daft Punk");
    EXPECT_EQ(drop_repeats("Skip the song.Playlist on Spotify."), "Skip the song.Playlist on Spotify.");
    EXPECT_EQ(drop_repeats("Play. Play something else."), "Play. Play something else.");
    EXPECT_EQ(drop_repeats("...Play music."), "...Play music.");
    EXPECT_EQ(drop_repeats(""), "");
}

TEST(Transcript, MaxTokensLeavesRoomForRealCommands) {
    // longest corpus command, ~3 s and ~15 tokens: "Buddy, play my no singing playlist on Spotify."
    EXPECT_GE(buddy::ears::max_tokens_for(3 * 16000), 30);
    EXPECT_EQ(buddy::ears::max_tokens_for(0), 10);
}
