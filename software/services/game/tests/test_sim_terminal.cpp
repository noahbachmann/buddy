// Terminal play (#51): a whole game typed into TerminalGame (option numbers, the prompt's candidate numbers), Dev's
// lines refused without changing anything, and the board text drawn from the payload alone.
#include <gtest/gtest.h>

#include "riftbound/ai/greedy.hpp"
#include "session_game.hpp"
#include "sim_terminal.hpp"

using nlohmann::json;

namespace {

game::HostConfig host_config() {
    game::HostConfig cfg;
    for (const auto& [key, file] : {std::pair{"vi", "vi-unleashed"}, std::pair{"zed", "zed-vendetta"}}) {
        const std::string text = deck_text(file);
        cfg.decks[key] = {parse_deck(db(), text), text};
    }
    cfg.champion.games = 0;  // the decklist's champion: self-play is slow under ASan (#98)
    return cfg;
}

game::TerminalGame terminal(const std::string& buddy, std::uint64_t seed) {
    return {db(), host_config(), {buddy, seed, table::parse_faults("stress"), std::make_unique<ai::GreedyPlayer>(seed)}};
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

// Dev types a random option number at every decision and random candidate numbers at every prompt (a refused card
// gets another try, as a person would): the game ends, and every printed board is the payload's.
TEST(RiftboundSimTerminal, TypedGamePlaysToTheEnd) {
    for (const auto& [buddy, seed] : {std::pair{"vi", 3}, std::pair{"zed", 4}}) {
        auto g = terminal(buddy, static_cast<std::uint64_t>(seed));
        std::string out = g.start();
        EXPECT_TRUE(has(out, "Buddy: \"Let's play")) << out;
        Rng rng(static_cast<std::uint64_t>(seed));
        int inputs = 0, prompts = 0, refused = 0;
        for (; !g.over() && inputs < 5000; ++inputs) {
            const json& st = g.host().state();
            if (!has(out, "✗")) {
                EXPECT_TRUE(out.ends_with(game::render(st))) << out;
            }
            std::string line;
            if (st.contains("prompt")) {
                ++prompts;
                const auto n = st["prompt"]["n"].get<std::size_t>();
                const auto& c = st["prompt"]["candidates"];
                for (std::size_t i = 0; i < n; ++i) line += std::to_string(rng.below(static_cast<std::uint32_t>(c.size()))) + " ";
            } else if (st.contains("options")) {
                line = std::to_string(rng.below(static_cast<std::uint32_t>(st["options"].size())));
            } else {
                FAIL() << "nothing to answer in " << st["status"];
            }
            out = g.input(line);
            if (has(out, "✗")) ++refused;
        }
        SCOPED_TRACE(std::string(buddy) + " seed " + std::to_string(seed));
        EXPECT_TRUE(g.over());
        EXPECT_TRUE(has(out, "Game over: ")) << out;
        EXPECT_GT(prompts, 3);
        EXPECT_LT(refused, inputs / 2);
    }
}

TEST(RiftboundSimTerminal, BadLinesChangeNothing) {
    auto g = terminal("vi", 7);
    g.start();
    while (!g.host().state().contains("options")) {
        ASSERT_TRUE(g.host().state().contains("prompt"));
        const auto& c = g.host().state()["prompt"]["candidates"];
        std::string line;
        for (std::size_t i = 0; i < g.host().state()["prompt"]["n"].get<std::size_t>(); ++i) line += c[0]["code"].get<std::string>() + " ";
        g.input(line);
    }
    const json before = g.host().state();
    for (const char* line : {"999", "99999999999999999999", "x", "1 2 3", "resume", "OGN-001"}) {
        EXPECT_TRUE(has(g.input(line), "✗")) << line;
        EXPECT_EQ(g.host().state(), before) << line;
    }
    EXPECT_TRUE(has(g.input("help"), "concede"));
    EXPECT_EQ(g.input("board"), game::render(before));
    EXPECT_TRUE(has(g.input("concede"), "Game over: Buddy wins"));
}

TEST(RiftboundSimTerminal, PromptAnswerByNumberOrCode) {
    const json p = {{"prompt", {{"kind", "deck"}, {"n", 2}, {"candidates", {{{"code", "OGN-001"}, {"left", 1}}, {{"code", "OGN-002"}, {"left", 2}}}}}}};
    EXPECT_EQ(game::prompt_answer(p, "1, 0"), (std::vector<std::string>{"OGN-002", "OGN-001"}));
    EXPECT_EQ(game::prompt_answer(p, "OGN-009 1"), (std::vector<std::string>{"OGN-009", "OGN-002"}));
    EXPECT_THROW(game::prompt_answer(p, "1"), std::invalid_argument);
    EXPECT_THROW(game::prompt_answer(p, "2 0"), std::invalid_argument);
    EXPECT_THROW(game::prompt_answer({{"status", "x"}}, "0"), std::invalid_argument);
}
