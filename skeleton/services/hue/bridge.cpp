#include "bridge.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "hue_ca.hpp"  // generated from root-bridge.pem

namespace buddy::hue {

namespace {

using json = nlohmann::json;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// "description" of the first error in a CLIP v2 ({"errors": [...]}) or v1 ([{"error": {...}}]) response
std::string error_of(const json& j) {
    if (j.is_object() && j.contains("errors") && j["errors"].is_array() && !j["errors"].empty())
        return j["errors"][0].value("description", "error");
    if (j.is_array() && !j.empty() && j[0].is_object() && j[0].contains("error"))
        return j[0]["error"].value("description", "error");
    return "";
}

json parse_ok(const HttpResponse& r, const std::string& what) {
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    const std::string error = j.is_discarded() ? "" : error_of(j);
    if (r.status < 200 || r.status >= 300 || !error.empty())
        throw BridgeError(r.status, what + ": HTTP " + std::to_string(r.status) + (error.empty() ? "" : ", " + error));
    if (j.is_discarded()) throw BridgeError(r.status, what + ": not JSON");
    return j;
}

} // namespace

std::optional<BridgeConfig> load_key_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    const auto j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) return std::nullopt;
    BridgeConfig cfg{.id = lower(j.value("bridge_id", "")), .address = j.value("address", ""), .app_key = j.value("app_key", "")};
    if (cfg.id.empty() || cfg.address.empty() || cfg.app_key.empty()) return std::nullopt;
    return cfg;
}

void save_key_file(const std::string& path, const BridgeConfig& cfg) {
    namespace fs = std::filesystem;
    if (const auto dir = fs::path(path).parent_path(); !dir.empty()) fs::create_directories(dir);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << json{{"bridge_id", cfg.id}, {"address", cfg.address}, {"app_key", cfg.app_key}}.dump(2) << "\n";
        if (!out) throw std::runtime_error("can't write " + tmp);
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    fs::rename(tmp, path);
}

CurlOptions Bridge::curl_options(const BridgeConfig& cfg) {
    return {.ca_pem = kHueRootCa, .resolve = {cfg.id + ":443:" + cfg.address}, .timeout_s = 5};
}

std::string Bridge::url(std::string_view path) const { return "https://" + cfg_.id + std::string(path); }

std::vector<std::string> Bridge::headers() const {
    return {"hue-application-key: " + cfg_.app_key, "Content-Type: application/json"};
}

json Bridge::resources() {
    const auto j = parse_ok(http_.request("GET", url("/clip/v2/resource"), headers()), "GET resources");
    if (!j.contains("data") || !j["data"].is_array()) throw BridgeError(200, "GET resources: no data");
    return j["data"];
}

void Bridge::send(const Request& r) {
    const std::string path = "/clip/v2/resource/" + r.type + (r.method == "POST" ? "" : "/" + r.id);
    const std::string body = r.method == "DELETE" ? "" : r.body.dump();
    parse_ok(http_.request(r.method, url(path), headers(), body), r.method + " " + r.type + " " + r.id);
}

std::optional<std::string> Bridge::pair(const std::string& device_type) {
    const json body = {{"devicetype", device_type}, {"generateclientkey", true}};
    const auto r = http_.request("POST", url("/api"), {"Content-Type: application/json"}, body.dump());
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_array() && !j.empty() && j[0].is_object()) {
        if (j[0].contains("success")) return j[0]["success"].value("username", "");
        if (j[0].contains("error") && j[0]["error"].value("type", 0) == 101) return std::nullopt;  // link button not pressed
    }
    throw BridgeError(r.status, "pairing: " + (j.is_discarded() ? r.body : j.dump()));
}

std::vector<BridgeConfig> discover(Http& http) {
    const auto r = http.request("GET", "https://discovery.meethue.com", {});
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (r.status != 200 || !j.is_array()) throw BridgeError(r.status, "discovery: HTTP " + std::to_string(r.status) + " " + r.body);
    std::vector<BridgeConfig> out;
    for (const auto& b : j)
        if (b.is_object() && b.contains("id") && b.contains("internalipaddress"))
            out.push_back({.id = lower(b.value("id", "")), .address = b.value("internalipaddress", ""), .app_key = ""});
    return out;
}

std::string bridge_id_at(Http& http, const std::string& address) {
    const auto r = http.request("GET", "http://" + address + "/api/config", {});
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (r.status != 200 || !j.is_object() || !j.contains("bridgeid"))
        throw BridgeError(r.status, "no Hue bridge at " + address);
    return lower(j.value("bridgeid", ""));
}

} // namespace buddy::hue
