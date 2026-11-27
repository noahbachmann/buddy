#include "parse.hpp"

#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace buddy::speakers {

namespace {

using json = nlohmann::json;

json parse_array(std::string_view text, const char* what) {
    auto j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_array()) throw std::runtime_error(std::string("pactl: unexpected ") + what + " output");
    return j;
}

std::string prop(const json& item, const char* key) {
    const auto props = item.find("properties");
    if (props == item.end() || !props->is_object()) return {};
    const auto v = props->find(key);
    return v != props->end() && v->is_string() ? v->get<std::string>() : std::string();
}

int index_of(const json& item, const char* key) {
    const auto v = item.find(key);
    if (v == item.end() || !v->is_number_integer()) return -1;
    const auto i = v->get<long long>();
    return i < 0 || i > 0x7fffffff ? -1 : static_cast<int>(i);  // PA_INVALID_INDEX (4294967295) = not linked
}

// bluetoothctl colours its output even into a pipe
std::string strip_ansi(std::string_view s) {
    static const std::regex ansi("\x1b\\[[0-9;]*m");
    return std::regex_replace(std::string(s), ansi, "");
}

std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> out;
    std::string clean = strip_ansi(text);
    size_t start = 0;
    while (start <= clean.size()) {
        const size_t end = clean.find('\n', start);
        std::string line = clean.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

} // namespace

std::vector<Sink> parse_sinks(std::string_view text) {
    std::vector<Sink> out;
    for (const auto& s : parse_array(text, "sinks")) {
        if (!s.is_object()) continue;
        Sink sink{.index = index_of(s, "index"), .name = s.value("name", ""), .mac = prop(s, "api.bluez5.address")};
        if (sink.index >= 0 && !sink.name.empty()) out.push_back(std::move(sink));
    }
    return out;
}

std::vector<Stream> parse_streams(std::string_view text) {
    std::vector<Stream> out;
    for (const auto& s : parse_array(text, "sink-inputs")) {
        if (!s.is_object()) continue;
        Stream stream{.index = index_of(s, "index"),
                      .sink = index_of(s, "sink"),
                      .node_name = prop(s, "node.name"),
                      .corked = s.value("corked", false)};
        if (stream.index >= 0) out.push_back(std::move(stream));
    }
    return out;
}

std::vector<BtDevice> parse_devices(std::string_view text) {
    static const std::regex device(R"(^Device ((?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}) (.+)$)");
    std::vector<BtDevice> out;
    std::smatch m;
    for (const auto& line : lines(text))
        if (std::regex_match(line, m, device)) out.push_back({m[1], m[2]});
    return out;
}

BtInfo parse_info(std::string_view text) {
    BtInfo info;
    for (auto line : lines(text)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (line.starts_with("Alias: ")) info.alias = line.substr(7);
        else if (line == "Connected: yes") info.connected = true;
        else if (line.starts_with("UUID: Audio Sink")) info.audio_sink = true;
    }
    return info;
}

} // namespace buddy::speakers
