#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "riftbound/deck.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

std::string read(const std::string& path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string zed() { return read(kDir + "/decks/zed-vendetta.txt"); }

std::string replace(std::string text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    EXPECT_NE(at, std::string::npos) << from;
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

std::vector<std::string> problems(const std::string& text) {
    try {
        parse_deck(fixture(), text);
    } catch (const DeckError& e) {
        return e.problems();
    }
    return {};
}

int count(const std::vector<DeckEntry>& entries, std::string_view name) {
    for (const auto& e : entries) {
        if (e.card->name == name) return e.count;
    }
    return 0;
}

} // namespace

TEST(RiftboundDeck, BothPrecons) {
    for (const auto& [file, legend, champion] : {
             std::tuple{"vi-unleashed", "Vi, Piltover Enforcer", "Vi, Hotheaded"},
             std::tuple{"zed-vendetta", "Zed, Master of Shadows", "Zed, From the Shadows"},
         }) {
        const Deck deck = load_deck(fixture(), kDir + "/decks/" + file + ".txt");
        EXPECT_EQ(deck.legend->name, legend);
        EXPECT_EQ(deck.champion->name, champion);
        EXPECT_EQ(deck.main_size(), 40) << file;
        EXPECT_EQ(deck.rune_count(), 12) << file;
        EXPECT_EQ(deck.battlefields.size(), 3u) << file;
    }
}

TEST(RiftboundDeck, Entries) {
    const Deck deck = parse_deck(fixture(), zed());
    EXPECT_EQ(count(deck.main, "Zed, From the Shadows"), 0);  // the chosen champion's copy isn't in main
    EXPECT_EQ(count(deck.main, "Blade Twirler"), 3);
    EXPECT_EQ(count(deck.runes, "Chaos Rune"), 6);
    EXPECT_EQ(deck.battlefields[0]->name, "Zaun Warrens");
}

TEST(RiftboundDeck, CodesAndLooseNames) {
    std::string text = replace(zed(), "3 Blade Twirler", "1 VEN-002\n2x blade twirler");
    text = replace(text, "6 Fury Rune", "3 ogn-007\n3 VEN • R01 • EN");
    const Deck deck = parse_deck(fixture(), text);
    EXPECT_EQ(count(deck.main, "Blade Twirler"), 3);
    EXPECT_EQ(count(deck.runes, "Fury Rune"), 6);
}

TEST(RiftboundDeck, SideboardIgnored) {
    EXPECT_EQ(problems(zed() + "\nSideboard:\n3 Arena Kingpin\n"), std::vector<std::string>{});
}

TEST(RiftboundDeck, UnknownCards) {
    std::string text = replace(zed(), "1 Death Mark", "1 Deth Mark");
    text = replace(text, "1 Wind and Ghosts", "1 VEN-999");
    EXPECT_EQ(problems(text), (std::vector<std::string>{
                                  "line 25: unknown card code VEN-999",
                                  "line 26: unknown card \"Deth Mark\" (did you mean \"Death Mark\"?)",
                                  "MainDeck: 38 cards with the champion, needs at least 40 (103.3)",
                              }));
}

TEST(RiftboundDeck, MalformedLines) {
    EXPECT_EQ(problems(replace(zed(), "1 Death Mark", "Death Mark"))[0],
              "line 26: expected \"<count> <card name>\", got \"Death Mark\"");
    EXPECT_EQ(problems(replace(zed(), "Runes:", "Rune Deck Two:"))[0], "line 33: unknown section \"Rune Deck Two:\"");
    EXPECT_EQ(problems("1 Zed, Master of Shadows\n" + zed())[0],
              "line 1: card before the first section (Legend:, MainDeck:, …)");
}

TEST(RiftboundDeck, CopiesCountTheChampion) {
    EXPECT_EQ(problems(replace(zed(), "1 Wind and Ghosts", "3 Zed, From the Shadows")),
              std::vector<std::string>{"Zed, From the Shadows: 4 copies, at most 3 (103.3.b)"});
}

TEST(RiftboundDeck, DomainIdentity) {
    // Vi's Order cards don't fit Zed's Fury+Chaos
    EXPECT_EQ(problems(replace(zed(), "1 Death Mark", "1 Vi, Peacekeeper")),
              std::vector<std::string>{
                  "Vi, Peacekeeper (Order) is outside the legend's domains Fury+Chaos (103.2.b)"});
    EXPECT_EQ(problems(replace(zed(), "6 Chaos Rune", "6 Order Rune")),
              std::vector<std::string>{"Order Rune (Order) is outside the legend's domains Fury+Chaos (103.2.b)"});
}

TEST(RiftboundDeck, ChosenChampion) {
    EXPECT_EQ(problems(replace(zed(), "1 Zed, From the Shadows", "1 Blade Twirler")),
              std::vector<std::string>{"Champion: Blade Twirler is not a champion unit (103.3.a.2)"});
    // a champion unit, but Morgana's
    EXPECT_EQ(problems(replace(replace(zed(), "1 Zed, From the Shadows", "1 Morgana, Vindictive"),
                               "1 Morgana, Vindictive\n1 Mel", "1 Zed, From the Shadows\n1 Mel")),
              std::vector<std::string>{
                  "Champion: Morgana, Vindictive doesn't have Zed, Master of Shadows's champion tag (103.3.a.2)"});
}

TEST(RiftboundDeck, SectionShapes) {
    const std::string text = replace(replace(replace(zed(), "6 Chaos Rune", "5 Chaos Rune"), "1 Zaun Warrens\n", ""),
                                     "1 Death Mark", "1 Fury Rune");
    EXPECT_EQ(problems(text), (std::vector<std::string>{
                                  "MainDeck: Fury Rune is a Rune",
                                  "Runes: 11 runes, needs exactly 12 (103.4.a)",
                                  "Battlefields: 2, needs 3 for 1v1 (103.5.a)",
                              }));
    EXPECT_EQ(problems(replace(zed(), "1 Zed, Master of Shadows", "1 Zed, Without a Sound")),
              std::vector<std::string>{"Legend: Zed, Without a Sound is not a legend"});
    EXPECT_EQ(problems(replace(zed(), "Legend:\n1 Zed, Master of Shadows\n", "")),
              std::vector<std::string>{"Legend: needs exactly 1 card, has 0"});
}

TEST(RiftboundDeck, LoadNamesTheFile) {
    try {
        load_deck(CardDb::parse(nlohmann::json{{"printings", nlohmann::json::array()}}), kDir + "/decks/zed-vendetta.txt");
        FAIL() << "no cards: every line is unknown";
    } catch (const DeckError& e) {
        EXPECT_EQ(e.problems()[0].rfind(kDir + "/decks/zed-vendetta.txt: line 2: unknown card", 0), 0u) << e.problems()[0];
    }
}
