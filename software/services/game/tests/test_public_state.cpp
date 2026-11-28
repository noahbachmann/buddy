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
            if ((k == "card" || k == "source") && v.is_string()) out.push_back(v.get<std::string>());
            else if ((k == "cards" || k == "known") && v.is_array()) for (const auto& c : v) out.push_back(c.get<std::string>());
            else card_codes(v, out);
        }
    } else if (j.is_array()) {
        for (const auto& v : j) card_codes(v, out);
    }
}

} // namespace

TEST(RiftboundPublicState, SameForEveryArrangementOfHiddenCards) {
    long compared = 0, effects = 0, linked = 0, known_runes = 0, to_base = 0, picked = 0;
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
            effects += want.dump().find("\"effects\"") != std::string::npos;
            for (const auto& l : want["log"]) linked += l.contains("cards");
            known_runes += want.contains("prompt") && want["prompt"].contains("known");
            for (const auto& o : want.value("options", nlohmann::json::array())) {
                const auto text = o["text"].get<std::string>();
                const bool says_base = text == "move to base" || text == "to base";
                if (o.contains("base") != says_base) g.problems.push_back("base flag on \"" + text + "\"");
                if (o.contains("base") && o.contains("obj")) g.problems.push_back("base option with an obj: " + text);
                to_base += o.contains("base");
            }
            if (want.contains("decision") && want["decision"].contains("picked")) {
                ++picked;
                const std::string all = want.dump();
                for (const auto& id : want["decision"]["picked"]) {
                    if (all.find("\"id\":" + id.dump()) == std::string::npos) g.problems.push_back("picked object not shown");
                }
            }
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
    EXPECT_GT(effects, 0);  // some payload showed a spell's effect on a unit (#97)
    EXPECT_GT(linked, 0);
    EXPECT_GT(known_runes, 0);
    EXPECT_GT(to_base, 0);
    EXPECT_GT(picked, 0);  // a Choose with picks so far (units to move), marked on the panel  // a move or a location to Dev's base, flagged for the base row  // a channel that brought back a rune recycled in view
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
