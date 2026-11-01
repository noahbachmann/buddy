// 1000 seeded random-vs-random games over the real decks without a failed check, under ASan/UBSan in
// deploy/test.sh --riftbound (and --pi): 5.2's "Done when" (#20) in vanilla mode, 5.3's (#33) with every card's
// rules. A failure writes its replay next to the test (buddy-game --replay <file>).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "riftbound/random_game.hpp"
#include "riftbound/replay.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

std::string deck_text(const std::string& name) {
    std::ifstream in(kDir + "/decks/" + name + ".txt");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void thousand_games(bool vanilla) {
    const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    const std::array<std::string, player_count> texts{deck_text("vi-unleashed"), deck_text("zed-vendetta")};
    const Deck vi = parse_deck(db, texts[0]), zed = parse_deck(db, texts[1]);
    int failed = 0;
    for (std::uint64_t seed = 0; seed < 1000 && failed < 3; ++seed) {
        const RandomGame g = play_random(vi, zed, seed, 100000, vanilla);
        if (g.problem.empty()) continue;
        ++failed;
        const auto file = std::filesystem::absolute(std::string("riftbound-replay-") + (vanilla ? "vanilla-" : "") +
                                                    std::to_string(seed) + ".txt");
        std::ofstream(file) << to_text({seed, vanilla, texts, g.log}, db);
        ADD_FAILURE() << "seed " << seed << ": " << g.problem << "\n  replay: " << file.string();
    }
}

} // namespace

TEST(RiftboundGate, ThousandRandomGames) { thousand_games(true); }

TEST(RiftboundGate, ThousandRandomGamesWithRules) { thousand_games(false); }
