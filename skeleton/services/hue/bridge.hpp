#pragma once
// Hue Bridge CLIP v2 API on the LAN (no cloud). Requests go to https://<bridge id>/…, resolved to the bridge's
// address (CURLOPT_RESOLVE): its certificate is signed by Hue's "root-bridge" CA (compiled in) with the bridge id
// as common name, so TLS is fully verified without a hostname in DNS.
//
// Pairing: the link button on the bridge, then POST /api within 30 s → application key (header
// hue-application-key on every request). Stored in the key file with the bridge id and address.

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "buddy/curl_http.hpp"
#include "buddy/http.hpp"

namespace buddy::hue {

struct BridgeConfig {
    std::string id;       // bridge id, lower case: "ecb5fafffe9164da"
    std::string address;  // LAN IP
    std::string app_key;  // from pairing; empty = not paired
};

// Key file (BUDDY_HUE_KEY_FILE, mode 600): {"bridge_id", "address", "app_key"}. nullopt = missing or broken.
std::optional<BridgeConfig> load_key_file(const std::string& path);
void save_key_file(const std::string& path, const BridgeConfig& cfg);

// The bridge answered with an error status (key revoked, unknown id, rate limit, …).
struct BridgeError : std::runtime_error {
    BridgeError(long status, const std::string& what) : std::runtime_error(what), status(status) {}
    long status;
};

// One request to /clip/v2/resource/<type>[/<id>]: PUT and DELETE name the resource, POST creates one (no id).
struct Request {
    std::string type;  // light, grouped_light, scene
    std::string id;
    nlohmann::json body;
    std::string method = "PUT";
};

class Bridge {
public:
    Bridge(BridgeConfig cfg, Http& http) : cfg_(std::move(cfg)), http_(http) {}

    // For the CurlHttp that talks to this bridge.
    static CurlOptions curl_options(const BridgeConfig& cfg);

    const BridgeConfig& config() const { return cfg_; }
    std::string url(std::string_view path) const;
    std::vector<std::string> headers() const;

    // GET /clip/v2/resource: every resource's JSON ("data"). Throws BridgeError, HttpError.
    nlohmann::json resources();
    // Throws BridgeError (with the bridge's error description), HttpError.
    void send(const Request& r);

    // POST /api with the link button pressed: the new application key, nullopt while the button isn't pressed.
    // Throws BridgeError, HttpError.
    std::optional<std::string> pair(const std::string& device_type);

private:
    BridgeConfig cfg_;
    Http& http_;
};

// Bridges on the LAN from https://discovery.meethue.com (rate limited by Hue: about one call per 15 min).
// Returns {id, address} pairs; ids lower case.
std::vector<BridgeConfig> discover(Http& http);

// Bridge id from http://<address>/api/config (public, no key): for pairing with a known address.
std::string bridge_id_at(Http& http, const std::string& address);

} // namespace buddy::hue
