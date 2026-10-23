#include "auth.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace buddy::spotify {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

const char* kAuthorizeUrl = "https://accounts.spotify.com/authorize";
const char* kTokenUrl = "https://accounts.spotify.com/api/token";
// Read the device list and current volume; control playback; find your own and followed playlists by name.
// Adding a scope needs a new --login.
const char* kScopes =
    "user-read-playback-state user-modify-playback-state playlist-read-private playlist-read-collaborative";

std::string base64url(const unsigned char* data, size_t len) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        const unsigned n = (data[i] << 16) | (i + 1 < len ? data[i + 1] << 8 : 0) | (i + 2 < len ? data[i + 2] : 0);
        out += abc[(n >> 18) & 63];
        out += abc[(n >> 12) & 63];
        if (i + 1 < len) out += abc[(n >> 6) & 63];
        if (i + 2 < len) out += abc[n & 63];
    }
    return out;  // no '=' padding, as PKCE requires
}

std::string random_string(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) throw std::runtime_error("RAND_bytes failed");
    return base64url(buf.data(), buf.size());
}

std::string sha256_base64url(const std::string& s) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(s.data(), s.size(), md, &len, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    return base64url(md, len);
}

std::string url_decode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += s[i] == '+' ? ' ' : s[i];
        }
    }
    return out;
}

// "http://127.0.0.1:8888/callback?code=AQ…&state=xyz" → {code: AQ…, state: xyz}
std::map<std::string, std::string> query_params(const std::string& url) {
    std::map<std::string, std::string> params;
    const auto q = url.find('?');
    if (q == std::string::npos) return params;
    std::string_view rest(url);
    rest.remove_prefix(q + 1);
    if (const auto hash = rest.find('#'); hash != std::string_view::npos) rest = rest.substr(0, hash);
    while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto pair = rest.substr(0, amp);
        const auto eq = pair.find('=');
        if (eq != std::string_view::npos) params[std::string(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        if (amp == std::string_view::npos) break;
        rest.remove_prefix(amp + 1);
    }
    return params;
}

} // namespace

Auth::Auth(AuthConfig cfg, Http& http) : cfg_(std::move(cfg)), http_(http) {
    std::ifstream f(cfg_.token_file);
    if (!f) return;
    const auto j = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (!j.is_discarded() && j.contains("refresh_token") && j["refresh_token"].is_string())
        refresh_token_ = j["refresh_token"];
    else  // a broken file must not crash the service at every start
        std::cout << "[spotify] " << cfg_.token_file << " has no refresh token: run buddy-spotify --login" << std::endl;
}

void Auth::login(std::istream& in, std::ostream& out) {
    const std::string verifier = random_string(64);  // 86 characters (PKCE allows 43–128)
    const std::string state = random_string(16);
    const std::string url = std::string(kAuthorizeUrl) + "?response_type=code" +
                            "&client_id=" + url_encode(cfg_.client_id) +
                            "&scope=" + url_encode(kScopes) +
                            "&redirect_uri=" + url_encode(cfg_.redirect_uri) +
                            "&code_challenge_method=S256" +
                            "&code_challenge=" + sha256_base64url(verifier) +
                            "&state=" + state;

    out << "1. Open this URL in a browser and log in to Spotify:\n\n" << url << "\n\n"
        << "2. The browser then fails to load " << cfg_.redirect_uri << "?code=… (expected).\n"
        << "   Copy the full URL from its address bar and paste it here:\n> " << std::flush;
    std::string pasted;
    std::getline(in, pasted);

    auto params = query_params(pasted);
    if (params.contains("error")) throw std::runtime_error("Spotify refused the login: " + params["error"]);
    if (!params.contains("code")) throw std::runtime_error("no ?code=… in the pasted URL");
    if (params["state"] != state) throw std::runtime_error("state mismatch: paste the URL from this login attempt");

    token_request("grant_type=authorization_code&code=" + url_encode(params["code"]) +
                  "&redirect_uri=" + url_encode(cfg_.redirect_uri) + "&client_id=" + url_encode(cfg_.client_id) +
                  "&code_verifier=" + verifier);
    out << "✓ logged in, token saved to " << cfg_.token_file << std::endl;
}

std::string Auth::access_token() {
    if (!access_token_.empty() && Clock::now() + std::chrono::seconds(60) < expires_) return access_token_;
    if (refresh_token_.empty()) throw AuthError("not logged in: run buddy-spotify --login");
    token_request("grant_type=refresh_token&refresh_token=" + url_encode(refresh_token_) +
                  "&client_id=" + url_encode(cfg_.client_id));
    return access_token_;
}

void Auth::token_request(const std::string& form) {
    const auto r = http_.request("POST", kTokenUrl, {"Content-Type: application/x-www-form-urlencoded"}, form);
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (r.status != 200 || j.is_discarded() || !j.contains("access_token")) {
        // 400 invalid_grant: refresh token revoked or expired (e.g. app access removed in your Spotify account)
        if (r.status == 400) throw AuthError("token request rejected: " + r.body + " (run buddy-spotify --login)");
        throw std::runtime_error("token request failed: HTTP " + std::to_string(r.status) + " " + r.body);
    }
    access_token_ = j["access_token"];
    expires_ = Clock::now() + std::chrono::seconds(j.value("expires_in", 3600));
    if (j.contains("refresh_token") && j["refresh_token"] != refresh_token_) {
        refresh_token_ = j["refresh_token"];
        save();
    }
}

void Auth::save() const {
    namespace fs = std::filesystem;
    const fs::path path(cfg_.token_file);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write);
        f << json{{"refresh_token", refresh_token_}}.dump() << "\n";
        if (!f.flush()) throw std::runtime_error("cannot write " + tmp.string());
    }
    fs::rename(tmp, path);  // atomic: a crash never leaves a half-written token file
}

} // namespace buddy::spotify
