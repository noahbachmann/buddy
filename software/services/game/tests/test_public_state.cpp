// The panel's payload (#50): the leak test (the payload is the same for every arrangement of Buddy's hidden cards
// and of Dev's placeholders, after every input of SimDev games) and the catalogue.
#include <gtest/gtest.h>

#include "public_state.hpp"
#include "riftbound/tokens.hpp"
#include "session_game.hpp"

namespace {

// Every "card" value in the payload, recursively.
void card_codes(const nlohmann::json& j, std::vector<std::string>& out) {
    if (j.is_object()) {
        for (const auto& [k, v] : j.items()) {
            if (k == "card" && v.is_string()) out.push_back(v.get<std::string>());
            else card_codes(v, out);
        }
    } else if (j.is_array()) {
        for (const auto& v : j) card_codes(v, out);
    }
}

} // namespace

TEST(RiftboundPublicState, SameForEveryArrangementOfHiddenCards) {
    long compared = 0;
    std::set<std::string> statuses;
    for (int i = 0; i < 4; ++i) {
        const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i % 2)];
        Game g(d0, d1, static_cast<PlayerId>(i / 2), 800 + static_cast<std::uint64_t>(i), {}, 9, true);
        const auto cat = game::catalogue(g.cfg.decks[0], g.cfg.decks[1]);
        Rng rng(99 + static_cast<std::uint64_t>(i));
        g.stop_when = [&] {
            const Session& s = *g.session;
            const auto want = game::public_state(s);
            statuses.insert(want["status"].get<std::string>());
            std::vector<std::string> codes;
            card_codes(want, codes);
            for (const auto& c : codes) {
                if (!cat.contains(c)) g.problems.push_back("card " + c + " isn't in the catalogue");
            }
            if (auto p = g.leak_problem(rng); !p.empty()) {
                g.problems.push_back(p);
                return true;
            }
            compared += 2;
            return false;
        };
        g.run();
        SCOPED_TRACE("game " + std::to_string(i));
        EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    }
    EXPECT_GT(compared, 1000);
    for (const char* s : {"setup", "dev_to_act", "dev_to_reveal", "thinking", "acting", "over"}) {
        EXPECT_TRUE(statuses.contains(s)) << s;
    }
}

TEST(RiftboundPublicState, CatalogueHoldsBothDecksAndTokens) {
    const auto cfg_decks = std::array{parse_deck(db(), deck_text("vi-unleashed")), parse_deck(db(), deck_text("zed-vendetta"))};
    const auto cat = game::catalogue(cfg_decks[0], cfg_decks[1]);
    std::size_t cards = 3;  // tokens
    std::set<const Card*> seen;
    for (const auto& d : cfg_decks) {
        std::vector<const Card*> all{d.legend, d.champion};
        for (const auto& e : d.main) all.push_back(e.card);
        for (const auto& e : d.runes) all.push_back(e.card);
        all.insert(all.end(), d.battlefields.begin(), d.battlefields.end());
        for (const Card* c : all) {
            const std::string code = c->home().code.str();
            ASSERT_TRUE(cat.contains(code)) << c->name;
            const auto& e = cat[code];
            EXPECT_EQ(e["name"], c->name);
            EXPECT_EQ(e["type"], std::string(to_string(c->type)));
            EXPECT_EQ(e["text"], c->text);
            EXPECT_EQ(e.contains("cost"), c->energy.has_value()) << c->name;
            EXPECT_EQ(e.contains("might"), c->might.has_value()) << c->name;
            if (seen.insert(c).second) ++cards;
        }
    }
    EXPECT_EQ(cat.size(), cards);
    EXPECT_EQ(cat["token:Bird"]["might"], 1);
    EXPECT_EQ(cat["token:Gold"]["type"], "Gear");
    // a rune knows its domain, a champion is marked
    const Card* champion = cfg_decks[0].champion;
    EXPECT_EQ(cat[champion->home().code.str()]["champion"], true);
    const Card* rune = cfg_decks[0].runes.front().card;
    EXPECT_EQ(cat[rune->home().code.str()]["domains"].size(), 1u);
}
