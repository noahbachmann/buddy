#include "buddy/topics.hpp"

namespace buddy::topic {

bool matches(std::string_view filter, std::string_view topic) {
    while (true) {
        const auto fpos = filter.find('/');
        const auto tpos = topic.find('/');
        const auto f = filter.substr(0, fpos);
        const auto t = topic.substr(0, tpos);

        if (f == "#") return true;
        if (f != "+" && f != t) return false;

        const bool filter_end = fpos == std::string_view::npos;
        const bool topic_end = tpos == std::string_view::npos;
        if (filter_end || topic_end) {
            // "a/#" also matches "a"
            if (!filter_end && topic_end) return filter.substr(fpos + 1) == "#";
            return filter_end && topic_end;
        }
        filter.remove_prefix(fpos + 1);
        topic.remove_prefix(tpos + 1);
    }
}

} // namespace buddy::topic
