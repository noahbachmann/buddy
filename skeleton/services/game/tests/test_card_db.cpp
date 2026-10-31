#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "riftbound/card_db.hpp"

using namespace buddy::riftbound;
using json = nlohmann::json;

namespace {

// tests/data/cards.json: every printing of the two decks' cards from Riftcodex (text removed).
const CardDb& fixture() {
    static const CardDb db = CardDb::load(std::string(BUDDY_SOURCE_DIR) + "/services/game/tests/data/cards.json");
    return db;
}

json printing(std::string id, std::string name, std::string type = "Unit", json extra = json::object()) {
    json p = {{"id", id},       {"name", name},     {"set", "OGN"},   {"type", type},  {"supertype", nullptr},
              {"domains", {"Fury"}}, {"energy", 2}, {"might", 2},     {"power", nullptr}, {"tags", json::array()},
              {"text", ""}};
    p.update(extra);
    return p;
}

json doc(json printings) {
    return {{"sets",
             {{{"id", "OGN"}, {"name", "Origins"}, {"published", "2025-10-31"}},
              {{"id", "OPP"}, {"name", "Riftbound Organized Play Promotional Cards"}, {"published", "2025-10-31"}},
              {{"id", "UNL"}, {"name", "Unleashed"}, {"published", "2026-05-08"}}}},
            {"printings", printings}};
}

} // namespace

TEST(RiftboundCardDb, CardNames) {
    EXPECT_EQ(card_name("Vi - Piltover Enforcer (Signature)"), "Vi, Piltover Enforcer");
    EXPECT_EQ(card_name("Annie - Dark Child (Metal)"), "Annie, Dark Child");
    EXPECT_EQ(card_name("Renekton, Rage Fueled (Alternate Art)"), "Renekton, Rage Fueled");
    EXPECT_EQ(card_name("Sprite (274) // Buff"), "Sprite (274) // Buff");
    EXPECT_EQ(card_name("Arena Kingpin"), "Arena Kingpin");
}

TEST(RiftboundCardDb, FixtureLoads) {
    const auto& db = fixture();
    EXPECT_EQ(db.cards().size(), 55u);  // unique names in both decklists
    EXPECT_TRUE(db.warnings().empty());
    for (const auto& c : db.cards()) EXPECT_EQ(c.conflict, "") << c.name;
}

TEST(RiftboundCardDb, HomePrinting) {
    const auto& db = fixture();
    // OGN before the VEN overnumbered reprint and the PR promo
    EXPECT_EQ(db.find_name("Vi, Destructive")->home().code.str(), "OGN-036");
    // main set before the OPP promo, though OPP is older
    EXPECT_EQ(db.find_name("Loyal Poro")->home().code.str(), "UNL-156");
    // lower number before the overnumbered one, plain before signature
    EXPECT_EQ(db.find_name("Vi, Piltover Enforcer")->home().code.str(), "UNL-187");
    EXPECT_EQ(db.find_name("Vi, Piltover Enforcer")->printings.size(), 3u);
}

TEST(RiftboundCardDb, Fields) {
    const auto* zed = fixture().find_name("zed from the shadows");
    ASSERT_NE(zed, nullptr);
    EXPECT_EQ(zed->name, "Zed, From the Shadows");
    EXPECT_EQ(zed->type, CardType::Unit);
    EXPECT_TRUE(zed->champion);
    EXPECT_EQ(zed->domains, domain::fury);
    EXPECT_EQ(zed->energy, 4);
    EXPECT_EQ(zed->power, 1);
    EXPECT_EQ(zed->might, 4);
    EXPECT_TRUE(zed->has_tag("Zed"));
    EXPECT_TRUE(zed->has_tag("Ionia"));  // only on some printings: merged

    const auto* legend = fixture().find_name("Zed, Master of Shadows");
    EXPECT_EQ(legend->type, CardType::Legend);
    EXPECT_FALSE(legend->champion);
    EXPECT_EQ(legend->domains, domain::fury | domain::chaos);
    EXPECT_EQ(domains_string(legend->domains), "Fury+Chaos");
    EXPECT_EQ(domains_string(fixture().find_name("Zaun Warrens")->domains), "Colorless");
}

TEST(RiftboundCardDb, FindCode) {
    const auto& db = fixture();
    const auto found = db.find_code("OGN • 007a/298");
    ASSERT_NE(found.card, nullptr) << found.error;
    EXPECT_EQ(found.card->name, "Fury Rune");
    EXPECT_EQ(found.printing->full_name, "Fury Rune (Alternate Art)");
    EXPECT_EQ(db.find_code("VEN • R01 • EN").card, found.card);
    EXPECT_EQ(db.find_code("unl-229*").card->name, "Vi, Piltover Enforcer");
}

TEST(RiftboundCardDb, FindCodeErrors) {
    const auto& db = fixture();
    EXPECT_EQ(db.find_code("OGN-999").error, "unknown card code OGN-999");
    EXPECT_EQ(db.find_code("Arena Kingpin").error, "\"Arena Kingpin\" is not a card code (like OGN-007a/298)");
    // right number, wrong set size: a misread
    const auto misread = db.find_code("OGN-007/219");
    EXPECT_EQ(misread.card, nullptr);
    EXPECT_EQ(misread.error, "OGN-007/219: no such printing (OGN-007 is Fury Rune), misread?");
}

TEST(RiftboundCardDb, PromosShareNumbers) {
    // OPP reuses numbers per base set: the total tells them apart
    const auto& db = fixture();
    EXPECT_EQ(db.find_code("OPP • 009/219").card->name, "Upstage Comedy");
    EXPECT_EQ(db.find_code("opp-009-221").card->name, "Serrated Dirk");
    EXPECT_EQ(db.find_code("OPP-009").error,
              "OPP-009 is ambiguous (OPP-009/221 is Serrated Dirk, OPP-009/219 is Upstage Comedy): give the /total");
    EXPECT_EQ(db.find_name("Serrated Dirk")->printings[1].label, "OPP-009/221");
    EXPECT_EQ(db.find_name("Serrated Dirk")->home().label, "SFD-009");
}

TEST(RiftboundCardDb, SimilarNames) {
    const auto& db = fixture();
    EXPECT_EQ(db.similar_names("Carion Dredger"), std::vector<std::string>{"Carrion Dredger"});
    EXPECT_EQ(db.find_name("Mageseeker, Investigator")->name, "Mageseeker Investigator");
    EXPECT_TRUE(db.similar_names("Teemo, Swift Scout").empty());
}

TEST(RiftboundCardDb, PrintingsOfOneCardMustAgree) {
    const auto db = CardDb::parse(doc({
        printing("ogn-001-298", "Brute"),
        printing("unl-001-219", "Brute", "Unit", {{"might", 3}}),
        printing("ogn-002-298", "Scout", "Unit", {{"tags", {"Ionia"}}}),
        printing("opp-002-298", "Scout", "Unit", {{"tags", {"Yordle"}}, {"text", "other wording"}}),
    }));
    EXPECT_EQ(db.find_name("Brute")->conflict, "OGN-001 is Unit Fury energy 2 might 2, UNL-001 is Unit Fury energy 2 might 3");
    const auto* scout = db.find_name("Scout");
    EXPECT_EQ(scout->conflict, "");
    EXPECT_EQ(scout->tags, (std::vector<std::string>{"Ionia", "Yordle"}));
}

TEST(RiftboundCardDb, BadPrintingsAreSkipped) {
    const auto db = CardDb::parse(doc({
        printing("ogn-001-298", "Fine"),
        printing("nonsense", "No Id"),
        printing("ogn-003-298", "New Kind", "Contraption"),
    }));
    EXPECT_EQ(db.cards().size(), 1u);
    EXPECT_EQ(db.warnings(), (std::vector<std::string>{"skipped nonsense: unreadable id",
                                                       "skipped ogn-003-298: unknown type Contraption"}));
}

TEST(RiftboundCardDb, NotACardFile) {
    EXPECT_THROW(CardDb::parse(json::object()), std::runtime_error);
    EXPECT_THROW(CardDb::load("/nonexistent/cards.json"), std::runtime_error);
}

TEST(RiftboundCardDb, PrintedTiming) {  // 806, 813: the card's leading keyword; "[>]" makes it an ability's
    EXPECT_EQ(printed_timing("[Reaction] (Play any time, even before spells and abilities resolve.)Choose …"),
              Timing::Reaction);
    EXPECT_EQ(printed_timing("[Action] (Play on your turn or in showdowns.)Deal 3 …"), Timing::Action);
    EXPECT_EQ(printed_timing("[Action][&gt;] Exhaust a unit you control …"), Timing::Normal);
    EXPECT_EQ(printed_timing("This enters exhausted.[Reaction][&gt;] …"), Timing::Normal);
    EXPECT_EQ(printed_timing(""), Timing::Normal);
    const auto& db = fixture();
    EXPECT_EQ(db.find_name("Tactical Retreat")->timing, Timing::Reaction);
    EXPECT_EQ(db.find_name("Ruthless Strike")->timing, Timing::Action);
    EXPECT_EQ(db.find_name("Soul Harvest")->timing, Timing::Normal);
}
