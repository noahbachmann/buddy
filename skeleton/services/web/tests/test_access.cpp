#include <gtest/gtest.h>

#include "access.hpp"

using namespace buddy::web;

TEST(Access, Hosts) {
    for (const char* ok : {"buddy.local", "buddy.local:8080", "Buddy.Local:8080", "192.168.1.72:8080", "localhost:8080",
                           "[fe80::1]:8080", "10.0.0.1"})
        EXPECT_TRUE(allowed_host(ok, {})) << ok;
    for (const char* bad : {"", "evil.com", "evil.com:8080", "buddy.local.evil.com", ".local", "1.2.3", "1.2.3.4.5", "1234.1.1.1"})
        EXPECT_FALSE(allowed_host(bad, {})) << bad;
    EXPECT_TRUE(allowed_host("buddy:8080", {"buddy"}));
}

TEST(Access, Origin) {
    EXPECT_TRUE(same_origin("http://buddy.local:8080", "buddy.local:8080"));
    EXPECT_FALSE(same_origin("http://evil.com", "buddy.local:8080"));
    EXPECT_FALSE(same_origin("http://buddy.local", "buddy.local:8080"));
    EXPECT_FALSE(same_origin("null", "buddy.local:8080"));
}

TEST(Access, CommandTopics) {
    EXPECT_EQ(command_topic("hue"), "buddy/hue/cmd");
    EXPECT_EQ(command_topic("spotify"), "buddy/spotify/cmd");
    EXPECT_EQ(command_topic("voice"), std::nullopt);   // voice/say isn't a …/cmd
    EXPECT_EQ(command_topic("motion"), std::nullopt);  // web: false (moves things)
    EXPECT_EQ(command_topic("ears"), std::nullopt);
}

TEST(Access, CommandBodies) {
    EXPECT_EQ(command_error(R"({"action": "on", "id": "x"})"), "");
    EXPECT_NE(command_error(R"({"action": ""})"), "");
    EXPECT_NE(command_error(R"({"action": 5})"), "");
    EXPECT_NE(command_error(R"(["on"])"), "");
    EXPECT_NE(command_error("on"), "");
}
