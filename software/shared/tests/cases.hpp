#pragma once
// Test cases both languages check, in shared/tests/cases/<name>.json (pytest reads the same files).

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace buddy::test {

// Returns one key's value by value: `for (… : cases("x", "cases"))` is safe, `for (… : load()["cases"])` would loop
// over a destroyed temporary (e0bf506).
inline nlohmann::json cases(const std::string& name, const std::string& key) {
    const std::string path = std::string(BUDDY_SOURCE_DIR) + "/shared/tests/cases/" + name + ".json";
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    return nlohmann::json::parse(in).at(key);
}

} // namespace buddy::test
