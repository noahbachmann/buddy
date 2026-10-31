// 5.2's "Done when" (#20): 1000 seeded random-vs-random games over the real decks (vanilla mode) without a failed
// check, under ASan/UBSan in deploy/test.sh --riftbound (and --pi). A failure writes its replay next to the test
// (buddy-game --replay <file>).
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

} // namespace

TEST(RiftboundGate, ThousandRandomGames) {
    const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    const std::array<std::string, player_count> texts{deck_text("vi-unleashed"), deck_text("zed-vendetta")};
    const Deck vi = parse_deck(db, texts[0]), zed = parse_deck(db, texts[1]);
    int failed = 0;
    for (std::uint64_t seed = 0; seed < 1000 && failed < 3; ++seed) {
        const RandomGame g = play_random(vi, zed, seed);
        if (g.problem.empty()) continue;
        ++failed;
        const auto file = std::filesystem::absolute("riftbound-replay-" + std::to_string(seed) + ".txt");
        std::ofstream(file) << to_text({seed, true, texts, g.log}, db);
        ADD_FAILURE() << "seed " << seed << ": " << g.problem << "\n  replay: " << file.string();
    }
}
