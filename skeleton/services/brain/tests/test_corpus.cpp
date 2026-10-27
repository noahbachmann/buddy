// Every train/test sentence in tools/voice/commands.tsv must give its intent, query and device: one test each.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rules.hpp"

namespace {

struct Line {
    int row;
    std::string sentence, intent, query, device;
};

void PrintTo(const Line& l, std::ostream* os) { *os << '"' << l.sentence << '"'; }  // ctest name, not a byte dump

std::vector<Line> corpus() {
    std::ifstream in(BUDDY_SOURCE_DIR "/tools/voice/commands.tsv");
    std::vector<Line> lines;
    int row = 0;
    for (std::string line; std::getline(in, line);) {
        ++row;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::istringstream fields(line);
        for (std::string f; std::getline(fields, f, '\t');) cols.push_back(f);
        if (cols.size() < 3 || (cols[0] != "train" && cols[0] != "test")) continue;
        cols.resize(5);
        lines.push_back({row, cols[1], cols[2], cols[3], cols[4]});
    }
    return lines;
}

class Corpus : public testing::TestWithParam<Line> {};

TEST_P(Corpus, Parses) {
    static const buddy::brain::RuleParser parser;
    const Line& l = GetParam();
    const auto got = parser.parse(l.sentence);
    EXPECT_EQ(got ? got->name : "unknown", l.intent) << "normalized: " << buddy::brain::normalize(l.sentence);
    EXPECT_EQ(got ? got->query : "", l.query);
    EXPECT_EQ(got ? got->device : "", l.device);
}

INSTANTIATE_TEST_SUITE_P(CommandsTsv, Corpus, testing::ValuesIn(corpus()),
                         [](const auto& info) { return "row" + std::to_string(info.param.row); });

TEST(Corpus, NotEmpty) { EXPECT_GT(corpus().size(), 100u); }  // wrong path = 0 sentences, which would pass silently

} // namespace
