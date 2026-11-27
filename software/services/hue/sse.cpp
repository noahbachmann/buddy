#include "sse.hpp"

namespace buddy::hue {

std::vector<std::string> SseParser::feed(std::string_view chunk) {
    std::vector<std::string> out;
    for (const char c : chunk) {
        if (c != '\n') {
            line_ += c;
            continue;
        }
        if (!line_.empty() && line_.back() == '\r') line_.pop_back();
        if (line_.empty()) {  // blank line ends the event
            if (has_data_) out.push_back(std::move(data_));
            data_.clear();
            has_data_ = false;
        } else if (line_.starts_with("data:")) {
            std::string_view value(line_);
            value.remove_prefix(5);
            if (value.starts_with(' ')) value.remove_prefix(1);
            if (has_data_) data_ += '\n';
            data_ += value;
            has_data_ = true;
        }
        line_.clear();
    }
    return out;
}

} // namespace buddy::hue
