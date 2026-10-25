// ctest topics: wildcard matching (same cases as shared/python/tests/test_topics.py) and the generated header.
#include <iostream>

#include "buddy/topics.hpp"

int main() {
    using buddy::topic::matches;
    struct Case {
        const char* filter;
        const char* topic;
        bool expected;
    };
    const Case cases[] = {
        {"buddy/+/status", "buddy/ears/status", true},
        {"buddy/#", "buddy/ears/state", true},
        {"buddy/#", "buddy", true},
        {"#", "anything/at/all", true},
        {"buddy/ears/state", "buddy/ears/state", true},
        {"buddy/+/status", "buddy/ears/state", false},
        {"buddy/+", "buddy/ears/state", false},
        {"buddy/ears/state/x", "buddy/ears/state", false},
    };
    int failed = 0;
    for (const auto& c : cases) {
        if (matches(c.filter, c.topic) == c.expected) continue;
        std::cout << "✗ matches(\"" << c.filter << "\", \"" << c.topic << "\") != " << c.expected << std::endl;
        ++failed;
    }
    if (buddy::topic::ears_state != "buddy/ears/state" || buddy::topic::status("ears") != "buddy/ears/status") {
        std::cout << "✗ generated topics.hpp" << std::endl;
        ++failed;
    }
    return failed == 0 ? 0 : 1;
}
