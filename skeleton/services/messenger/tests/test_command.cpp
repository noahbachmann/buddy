#include <gtest/gtest.h>

#include "command.hpp"

using namespace buddy::messenger;

TEST(Command, Notify) {
    const auto n = std::get<Notify>(parse_command(R"({"action": "notify", "text": "  Laundry is done \n"})"));
    EXPECT_EQ(n.text, "Laundry is done");
    EXPECT_TRUE(n.alert);
    EXPECT_EQ(n.key, "");
    const auto quiet = std::get<Notify>(parse_command(R"({"action": "notify", "text": "ok", "level": "info", "key": "k"})"));
    EXPECT_FALSE(quiet.alert);
    EXPECT_EQ(quiet.key, "k");
}

TEST(Command, Ask) {
    const auto a = std::get<Ask>(parse_command(
        R"({"action": "ask", "id": "t1", "text": "Tea or coffee?", "options": ["tea", " coffee "], "from": "brain", "ttl_s": 600})"));
    EXPECT_EQ(a.id, "t1");
    EXPECT_EQ(a.text, "Tea or coffee?");
    EXPECT_EQ(a.options, (std::vector<std::string>{"tea", "coffee"}));
    EXPECT_EQ(a.from, "brain");
    EXPECT_EQ(a.ttl_s, 600);
    const auto plain = std::get<Ask>(parse_command(R"({"action": "ask", "id": "t2", "text": "When?"})"));
    EXPECT_TRUE(plain.options.empty());
    EXPECT_EQ(plain.ttl_s, std::nullopt);
    EXPECT_EQ(std::get<Ask>(parse_command(R"({"action": "ask", "id": "t3", "text": "?", "ttl_s": 0})")).ttl_s, 0);
}

TEST(Command, Cancel) { EXPECT_EQ(std::get<Cancel>(parse_command(R"({"action": "cancel", "id": "t1"})")).id, "t1"); }

TEST(Command, RejectsWhatItCantUse) {
    for (const char* payload : {
             "nope",
             "[]",
             R"({"text": "x"})",
             R"({"action": "shout", "text": "x"})",
             R"({"action": "notify"})",
             R"({"action": "notify", "text": "   "})",
             R"({"action": "notify", "text": 5})",
             R"({"action": "notify", "text": "x", "level": "loud"})",
             R"({"action": "ask", "text": "x"})",
             R"({"action": "ask", "id": "", "text": "x"})",
             R"({"action": "ask", "id": "a", "text": "x", "options": "tea"})",
             R"({"action": "ask", "id": "a", "text": "x", "options": ["tea", ""]})",
             R"({"action": "ask", "id": "a", "text": "x", "options": ["1","2","3","4","5","6","7","8","9"]})",
             R"({"action": "ask", "id": "a", "text": "x", "ttl_s": -1})",
             R"({"action": "ask", "id": "a", "text": "x", "ttl_s": "soon"})",
             R"({"action": "cancel"})",
         }) {
        EXPECT_THROW(parse_command(payload), std::invalid_argument) << payload;
    }
    EXPECT_THROW(parse_command(R"({"action": "cancel", "id": ")" + std::string(kMaxId + 1, 'a') + R"("})"), std::invalid_argument);
}

TEST(Command, ErrorsNeverQuoteThePayload) {
    try {
        parse_command(R"({"action": "ask", "text": "a private question"})");
        FAIL();
    } catch (const std::invalid_argument& e) {
        EXPECT_EQ(std::string(e.what()).find("private"), std::string::npos);
    }
}

TEST(Command, LongTextsAreClipped) {
    const auto n = std::get<Notify>(parse_command(R"({"action": "notify", "text": ")" + std::string(5000, 'a') + R"("})"));
    EXPECT_EQ(n.text.size(), kMaxText);
    EXPECT_TRUE(n.text.ends_with("…"));
}

TEST(Clip, CutsBetweenCharacters) {
    EXPECT_EQ(clip("hello", 5), "hello");
    EXPECT_EQ(clip("hello world", 8), "hello…");
    EXPECT_EQ(clip("aäöü", 6), "aä…");   // 7 bytes; ö would end at byte 5, leaving no room for the ellipsis
    EXPECT_EQ(clip("ääää", 7), "ää…");
    EXPECT_EQ(clip("hello", 2), "…");
    EXPECT_EQ(trim(" \t a b \r\n"), "a b");
    EXPECT_EQ(trim("  "), "");
}
