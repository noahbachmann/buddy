#include <gtest/gtest.h>

#include "riftbound/code.hpp"

using namespace buddy::riftbound;

namespace {

std::string canonical(std::string_view text) {
    const auto code = parse_code(text);
    return code ? code->str() : "-";
}

} // namespace

TEST(RiftboundCode, PrintedOnTheCard) {
    EXPECT_EQ(canonical("OGN • 007a/298"), "OGN-007a");
    EXPECT_EQ(canonical("UNL • 229*/219"), "UNL-229*");
    EXPECT_EQ(canonical("UNL • 030/219"), "UNL-030");
    EXPECT_EQ(canonical("VEN • R01 • EN"), "VEN-R01");
    EXPECT_EQ(canonical("OGN · 142/298"), "OGN-142");
}

TEST(RiftboundCode, RiftcodexIds) {
    EXPECT_EQ(canonical("ogn-007a-298"), "OGN-007a");
    EXPECT_EQ(canonical("unl-229*-219"), "UNL-229*");
    EXPECT_EQ(canonical("ven-r01"), "VEN-R01");
    EXPECT_EQ(canonical("ven-sp1-006"), "VEN-SP1");
    EXPECT_EQ(canonical("sfd-t03"), "SFD-T03");
}

TEST(RiftboundCode, LooseSpellings) {
    EXPECT_EQ(canonical("ogn 7a"), "OGN-007a");
    EXPECT_EQ(canonical("OGN-007A"), "OGN-007a");
    EXPECT_EQ(canonical("  OGN - 142 / 298  "), "OGN-142");
    EXPECT_EQ(canonical("VEN-R1"), "VEN-R01");
    EXPECT_EQ(canonical("OGN-142 EN"), "OGN-142");
}

TEST(RiftboundCode, Total) {
    EXPECT_EQ(parse_code("OGN • 007a/298")->total, 298);
    EXPECT_EQ(parse_code("ven-sp1-006")->total, 6);
    EXPECT_EQ(parse_code("VEN • R01 • EN")->total, 0);
    EXPECT_EQ(parse_code("OGN-142")->total, 0);
}

TEST(RiftboundCode, NotACode) {
    for (const char* text : {"", "OGN", "OGN-", "142/298", "O-142", "OGN-1420", "OGN-142/2980", "OGN-142ab",
                             "Arena Kingpin", "OGN-142 ENG", "OGN-142/298 x", "OGN142"}) {
        EXPECT_FALSE(parse_code(text)) << text;
    }
}
