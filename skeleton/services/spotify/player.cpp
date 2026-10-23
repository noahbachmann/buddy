#include "player.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace buddy::spotify {

namespace {

using json = nlohmann::json;

const char* kApi = "https://api.spotify.com/v1";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Letters and digits only: "AC/DC" == "ac dc", "Guns N' Roses" == "guns n roses"
std::string squash(const std::string& s) {
    std::string out;
    for (const unsigned char c : s)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

// First non-null item of search result list `type` ("tracks", "artists", …), or null.
// Spotify sometimes returns null entries in playlist results.
json first_item(const json& results, const char* type) {
    if (!results.contains(type) || !results[type].contains("items")) return nullptr;
    for (const auto& item : results[type]["items"])
        if (item.is_object()) return item;
    return nullptr;
}

std::string error_reason(const HttpResponse& r) {
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("error") || !j["error"].is_object()) return r.body;
    return j["error"].value("reason", j["error"].value("message", r.body));
}

Result failed(std::string say) {
    Result r;
    r.ok = false;
    r.say = std::move(say);
    return r;
}

Result playing(bool on, std::string say = "") {
    Result r;
    r.say = std::move(say);
    r.playing = on;
    return r;
}

Result volume_set(int volume) {
    Result r;
    r.volume = volume;
    return r;
}

} // namespace

Player::Player(PlayerConfig cfg, Auth& auth, Http& http) : cfg_(std::move(cfg)), auth_(auth), http_(http) {}

Result Player::run(const std::string& action, const std::string& query) {
    try {
        return dispatch(action, query);
    } catch (const Failure& e) {
        std::cout << "[spotify] " << action << ": " << e.what() << std::endl;
        return failed(e.say);
    } catch (const AuthError& e) {
        std::cout << "[spotify] " << action << ": " << e.what() << std::endl;
        return failed("Spotify isn't logged in.");
    } catch (const HttpError& e) {
        std::cout << "[spotify] " << action << ": " << e.what() << std::endl;
        return failed("I can't reach Spotify.");
    } catch (const std::exception& e) {
        std::cout << "[spotify] " << action << ": " << e.what() << std::endl;
        return failed("Something went wrong with Spotify.");
    }
}

Result Player::dispatch(const std::string& action, const std::string& query) {
    const Device d = device();
    const std::string on = "?device_id=" + d.id;

    if (action == "play") return play(d, query);
    if (action == "pause") {
        const auto r = api("PUT", "/me/player/pause" + on);
        if (r.status != 403) expect_ok(r, "pause");  // 403 = already paused
        return playing(false);
    }
    if (action == "next" || action == "previous") {
        expect_ok(api("POST", "/me/player/" + action + on), action);
        return playing(true);
    }
    if (action == "volume_up" || action == "volume_down") {
        const int step = action == "volume_up" ? cfg_.volume_step : -cfg_.volume_step;
        const int volume = std::clamp(d.volume + step, 0, 100);
        expect_ok(api("PUT", "/me/player/volume?volume_percent=" + std::to_string(volume) + "&device_id=" + d.id),
                  "volume");
        return volume_set(volume);
    }
    throw Failure("unknown action \"" + action + "\"", "I can't do that with Spotify.");
}

Result Player::play(const Device& d, const std::string& query) {
    const std::string on = "?device_id=" + d.id;

    if (!query.empty()) {
        // "… playlist" searches playlists only; otherwise an exact artist name wins, then the best track.
        std::string q = lower(query);
        const bool want_playlist = q.size() > 9 && q.ends_with(" playlist");
        if (want_playlist) q.resize(q.size() - 9);
        const auto r = api("GET", "/search?q=" + url_encode(q) + "&type=" +
                                      (want_playlist ? "playlist" : "artist,track,playlist") + "&limit=1");
        expect_ok(r, "search");
        const json results = json::parse(r.body);

        json body;
        std::string say;
        const json artist = first_item(results, "artists");
        const json track = first_item(results, "tracks");
        const json playlist = first_item(results, "playlists");
        if (!artist.is_null() && !want_playlist && squash(artist["name"]) == squash(q)) {
            body = {{"context_uri", artist["uri"]}};
            say = "Playing " + artist["name"].get<std::string>() + ".";
        } else if (!track.is_null() && !want_playlist) {
            body = {{"uris", {track["uri"]}}};
            say = "Playing " + track["name"].get<std::string>();
            if (!track["artists"].empty()) say += " by " + track["artists"][0]["name"].get<std::string>();
            say += ".";
        } else if (!playlist.is_null()) {
            body = {{"context_uri", playlist["uri"]}};
            say = "Playing the playlist " + playlist["name"].get<std::string>() + ".";
        } else {
            throw Failure("no search result for \"" + q + "\"", "I couldn't find " + query + " on Spotify.");
        }
        expect_ok(api("PUT", "/me/player/play" + on, body), "play");
        return playing(true, say);
    }

    // Resume. If Buddy isn't the active device yet, transfer playback to it (continues where the
    // phone/PC left off). With nothing to resume, fall back to BUDDY_SPOTIFY_DEFAULT_URI.
    const auto r = d.active ? api("PUT", "/me/player/play" + on)
                            : api("PUT", "/me/player", {{"device_ids", {d.id}}, {"play", true}});
    if (r.status / 100 != 2 && !cfg_.default_uri.empty()) {
        std::cout << "[spotify] nothing to resume (" << r.status << " " << error_reason(r) << "), playing "
                  << cfg_.default_uri << std::endl;
        expect_ok(api("PUT", "/me/player/play" + on, {{"context_uri", cfg_.default_uri}}), "play default");
    } else {
        expect_ok(r, "resume");
    }
    return playing(true);
}

Player::Device Player::device() {
    const auto r = api("GET", "/me/player/devices");
    expect_ok(r, "devices");
    const json devices = json::parse(r.body);
    for (const auto& d : devices["devices"]) {
        if (lower(d.value("name", "")) != lower(cfg_.device_name) || !d["id"].is_string()) continue;
        return {.id = d["id"], .active = d.value("is_active", false),
                .volume = d["volume_percent"].is_number() ? d["volume_percent"].get<int>() : 50};
    }
    throw Failure("device \"" + cfg_.device_name + "\" not found (librespot not running or not logged in?)",
                  "Spotify isn't running on " + cfg_.device_name + ".");
}

HttpResponse Player::api(const std::string& method, const std::string& path, const json& body) {
    for (int attempt = 0;; ++attempt) {
        std::vector<std::string> headers = {"Authorization: Bearer " + auth_.access_token()};
        if (!body.is_null()) headers.emplace_back("Content-Type: application/json");
        auto r = http_.request(method, kApi + path, headers, body.is_null() ? "" : body.dump());
        if (r.status == 401 && attempt == 0) {  // access token expired early or revoked: refresh once
            auth_.invalidate();
            continue;
        }
        return r;
    }
}

void Player::expect_ok(const HttpResponse& r, const std::string& what) {
    if (r.status / 100 == 2) return;
    const std::string reason = error_reason(r);
    const std::string detail = what + ": HTTP " + std::to_string(r.status) + " " + reason;
    if (reason == "PREMIUM_REQUIRED") throw Failure(detail, "Spotify Premium is required for that.");
    if (r.status == 429) throw Failure(detail, "Spotify is busy, try again in a minute.");
    if (r.status == 404) throw Failure(detail, "There's nothing to play on " + cfg_.device_name + ".");
    throw Failure(detail, "Spotify didn't accept that.");
}

} // namespace buddy::spotify
