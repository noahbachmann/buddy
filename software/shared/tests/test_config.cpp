#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

#include "buddy/config.hpp"
#include "cases.hpp"

namespace {

constexpr const char* kVar = "BUDDY_TEST_VALUE";

// Runs every [value, result] case of `kind` through `get`, which returns the parsed value or the fallback.
template <typename T, typename Get>
void check(const char* kind, T fallback, Get get) {
    for (const auto& c : buddy::test::cases("env_values", kind)) {
        const auto value = c[0].get<std::string>();
        setenv(kVar, value.c_str(), 1);
        if (c[1] == "error") {
            EXPECT_THROW(get(fallback), std::invalid_argument) << '"' << value << '"';
        } else {
            EXPECT_EQ(get(fallback), c[1].is_null() ? fallback : c[1].get<T>()) << '"' << value << '"';
        }
    }
    unsetenv(kVar);
    EXPECT_EQ(get(fallback), fallback) << "unset";
}

} // namespace

TEST(Config, SharedCases) {
    check<std::string>("string", "fallback", [](std::string f) { return buddy::env_string(kVar, f); });
    check<int>("int", 1234, [](int f) { return buddy::env_int(kVar, f); });
    check<float>("float", 12.5f, [](float f) { return buddy::env_float(kVar, f); });
}

TEST(Config, ErrorNamesTheVariable) {
    setenv(kVar, "4o", 1);
    try {
        buddy::env_int(kVar, 1);
        FAIL() << "no exception";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find(kVar), std::string::npos) << e.what();
    }
    unsetenv(kVar);
}
