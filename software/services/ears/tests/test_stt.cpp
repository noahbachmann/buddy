#include <gtest/gtest.h>

#include "cases.hpp"
#include "stt.hpp"

TEST(Stt, AutoAudioCtxMatchesTraining) {  // tools/voice trains short-context models with the same formula
    for (const auto& c : buddy::test::cases("audio_ctx", "cases"))
        EXPECT_EQ(buddy::ears::auto_audio_ctx(c[0].get<std::size_t>()), c[1].get<int>()) << c[0];
}
