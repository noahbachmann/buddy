#pragma once
// Server-sent events parser: bytes in any chunking → the data of each complete event. Only "data:" fields
// count (joined with \n); "id:", "event:", "retry:" and comments (":") are skipped.

#include <string>
#include <string_view>
#include <vector>

namespace buddy::hue {

class SseParser {
public:
    std::vector<std::string> feed(std::string_view chunk);

private:
    std::string line_;  // incomplete line
    std::string data_;  // data of the current event
    bool has_data_ = false;
};

} // namespace buddy::hue
