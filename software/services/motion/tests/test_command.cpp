#include <gtest/gtest.h>

#include "command.hpp"

using namespace buddy::motion;

namespace {

Command ok(std::string_view payload) {
    auto c = parse_command(payload);
    EXPECT_TRUE(std::holds_alternative<Command>(c)) << payload << ": " << std::get<std::string>(c);
    return std::holds_alternative<Command>(c) ? std::get<Command>(c) : Command{};
}

bool bad(std::string_view payload) { return std::holds_alternative<std::string>(parse_command(payload)); }

} // namespace

TEST(MotionCommand, Move) {
    const auto c = ok(R"({"action":"move","joints":{"demo":30,"head_tilt":-10.5},"ms":400})");
    EXPECT_EQ(c.action, "move");
    EXPECT_EQ(c.joints, (std::map<std::string, float>{{"demo", 30}, {"head_tilt", -10.5f}}));
    EXPECT_EQ(c.ms, 400);
    EXPECT_FALSE(ok(R"({"action":"move","joints":{"demo":1}})").ms);
}

TEST(MotionCommand, CenterRelaxGesture) {
    EXPECT_TRUE(ok(R"({"action":"center"})").joints.empty());
    EXPECT_EQ(ok(R"({"action":"center","joints":{"demo":null}})").joints.size(), 1u);  // values ignored
    EXPECT_EQ(ok(R"({"action":"relax"})").action, "relax");
    EXPECT_EQ(ok(R"({"action":"gesture","query":"shake"})").query, "shake");  // as brain routes it
}

TEST(MotionCommand, Rejects) {
    for (const char* p : {"", "nope", "[]", R"({"joints":{}})", R"({"action":"dance"})", R"({"action":"move"})",
                          R"({"action":"move","joints":{"demo":"left"}})", R"({"action":"move","joints":[1]})",
                          R"({"action":"move","joints":{"demo":1},"ms":-5})", R"({"action":"move","joints":{"demo":1},"ms":1.5})",
                          R"({"action":"gesture"})", R"({"action":"gesture","query":5})"})
        EXPECT_TRUE(bad(p)) << p;
}
