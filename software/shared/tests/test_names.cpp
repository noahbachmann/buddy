#include <gtest/gtest.h>

#include "buddy/names.hpp"

using namespace buddy::names;

TEST(Names, SpokenName) {
    EXPECT_EQ(spoken_name("the Living Room"), "living room");
    EXPECT_EQ(spoken_name("my pixel eight"), "pixel 8");
}

TEST(Names, BestMatches) {
    const std::vector<std::string> names = {"Living room", "Bedroom", "Desk lamp", "Bedroom lamp"};
    EXPECT_EQ(best_matches(names, "living room"), std::vector<std::size_t>{0});
    EXPECT_EQ(best_matches(names, "the livin room"), std::vector<std::size_t>{0});  // close words count
    EXPECT_EQ(best_matches(names, "desk"), std::vector<std::size_t>{2});            // contained
    EXPECT_EQ(best_matches(names, "bedroom"), std::vector<std::size_t>{1});         // exact beats contained
    EXPECT_EQ(best_matches(names, "lamp"), (std::vector<std::size_t>{2, 3}));       // ambiguous
    EXPECT_TRUE(best_matches(names, "kitchen").empty());
}
