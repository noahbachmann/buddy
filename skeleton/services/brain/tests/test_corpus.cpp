// ctest brain_corpus: every train/test sentence in tools/voice/commands.tsv must give its intent, query and device.
//   buddy-brain-test <commands.tsv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "rules.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <commands.tsv>" << std::endl;
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot read " << argv[1] << std::endl;
        return 2;
    }
    const buddy::brain::RuleParser parser;
    int total = 0, failed = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::istringstream fields(line);
        for (std::string f; std::getline(fields, f, '\t');) cols.push_back(f);
        if (cols.size() < 3 || (cols[0] != "train" && cols[0] != "test")) continue;
        const std::string& sentence = cols[1];
        const std::string& intent = cols[2];
        const std::string query = cols.size() > 3 ? cols[3] : "";
        const std::string device = cols.size() > 4 ? cols[4] : "";

        ++total;
        const auto got = parser.parse(sentence);
        const std::string got_intent = got ? got->name : "unknown";
        const std::string got_query = got ? got->query : "";
        const std::string got_device = got ? got->device : "";
        if (got_intent == intent && got_query == query && got_device == device) continue;
        ++failed;
        std::cout << "✗ \"" << sentence << "\"\n    want " << intent << " \"" << query << "\" on \"" << device
                  << "\"\n     got " << got_intent << " \"" << got_query << "\" on \"" << got_device
                  << "\"   (normalized: \"" << buddy::brain::normalize(sentence) << "\")\n";
    }
    std::cout << total - failed << " / " << total << " sentences parsed as expected" << std::endl;
    return failed == 0 && total > 0 ? 0 : 1;
}
