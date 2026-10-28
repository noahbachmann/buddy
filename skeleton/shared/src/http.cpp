#include "buddy/http.hpp"

#include <cctype>

namespace buddy {

std::string url_encode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[u >> 4];
            out += hex[u & 15];
        }
    }
    return out;
}

} // namespace buddy
