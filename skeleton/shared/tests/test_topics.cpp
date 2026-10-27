#include <gtest/gtest.h>

#include "buddy/topics.hpp"
#include "cases.hpp"

TEST(Topics, MatchesSharedCases) {
    for (const auto& c : buddy::test::cases("topic_matches", "cases")) {
        const auto filter = c[0].get<std::string>(), topic = c[1].get<std::string>();
        EXPECT_EQ(buddy::topic::matches(filter, topic), c[2].get<bool>()) << filter << " vs " << topic;
    }
}

TEST(Topics, GeneratedHeader) {
    EXPECT_EQ(buddy::topic::ears_state, "buddy/ears/state");
    EXPECT_EQ(buddy::topic::status("ears"), "buddy/ears/status");
}
