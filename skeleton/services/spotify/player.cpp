#include "player.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

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

// squash() without a leading "the"/"some": "some strokes" and "The Strokes" are the same artist
std::string core_name(const std::string& s) {
    const std::string l = lower(s);
    for (const char* lead : {"the ", "some "})
        if (l.starts_with(lead)) return squash(l.substr(std::string(lead).size()));
    return squash(l);
}

std::string error_reason(const HttpResponse& r) {
    const auto j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("error") || !j["error"].is_object()) return r.body;
    return j["error"].value("reason", j["error"].value("message", r.body));
}

// Among the first non-null items of list `type`, the first whose name equals `name` (ignoring case,
// punctuation and a leading "the"/"some"), else the first one, else null. Spotify's top hit is often a related song or artist.
json best_item(const json& results, const char* type, const std::string& name) {
    if (!results.contains(type) || !results[type].contains("items")) return nullptr;
    json first = nullptr;
    for (const auto& item : results[type]["items"]) {
        if (!item.is_object()) continue;
        if (core_name(item.value("name", "")) == core_name(name)) return item;
        if (first.is_null()) first = item;
    }
    return first;
}

// Levenshtein distance: letters to insert, delete or change to turn a into b.
size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// How well a playlist name matches what was said: exact (ignoring case/punctuation) > contains it >
// per spoken word: 2 if it's in the name, 1 if a name word is close to it (whisper: "type" for "hype").
// 0 = no match. Tolerates whisper dropping, adding or slightly misspelling a word.
int match_score(const std::string& name, const std::string& spoken) {
    if (squash(name) == squash(spoken)) return 1000;
    if (!squash(spoken).empty() && squash(name).find(squash(spoken)) != std::string::npos) return 500;
    auto words = [](const std::string& s) {
        std::vector<std::string> out;
        std::string w;
        for (const unsigned char c : s + " ") {
            if (std::isalnum(c)) w += static_cast<char>(std::tolower(c));
            else if (!w.empty()) out.push_back(std::exchange(w, ""));
        }
        return out;
    };
    const auto in_name = words(name);
    int score = 0;
    for (const auto& w : words(spoken)) {
        if (std::find(in_name.begin(), in_name.end(), w) != in_name.end()) {
            score += 2;
            continue;
        }
        const size_t allowed = w.size() >= 7 ? 2 : w.size() >= 3 ? 1 : 0;  // "a"/"my" must match exactly
        for (const auto& n : in_name) {
            if (allowed > 0 && edit_distance(w, n) <= allowed) {
                score += 1;
                break;
            }
        }
    }
    return score;
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

Result said(std::string say) {
    Result r;
    r.say = std::move(say);
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

void Player::duck() {
    if (duck_) return;
    if (cfg_.duck_percent >= 100 || duck_disallowed_) return warm_up();
    try {
        // The active device and whether it plays, in one request. 204 = nothing active.
        const auto r = api("GET", "/me/player");
        if (r.status != 204) expect_ok(r, "player");
        const json p = r.status == 204 ? json::object() : json::parse(r.body);
        const json d = p.value("device", json());
        if (!d.is_object() || !d["id"].is_string() || lower(d.value("name", "")) != lower(cfg_.device_name))
            return warm_up();  // box not active: nothing to duck
        const Device box{.id = d["id"], .active = true,
                         .volume = d["volume_percent"].is_number() ? d["volume_percent"].get<int>() : 50};
        last_id_ = box.id;
        warm_device_ = box;
        warmed_at_ = std::chrono::steady_clock::now();
        if (!p.value("is_playing", false) || box.volume == 0) return;

        const int to = box.volume * cfg_.duck_percent / 100;
        const auto v = api("PUT", "/me/player/volume?volume_percent=" + std::to_string(to) + "&device_id=" + box.id);
        if (v.status == 403) {  // VOLUME_CONTROL_DISALLOW: some speakers only take volume from their own app
            duck_disallowed_ = true;
            std::cout << "[spotify] " << cfg_.device_name << " refuses remote volume (" << error_reason(v)
                      << "): ducking off" << std::endl;
            return;
        }
        expect_ok(v, "duck");
        duck_ = Duck{.restore_to = box.volume, .ducked_to = to};
        save_duck();
    } catch (const std::exception& e) {
        std::cout << "[spotify] duck: " << e.what() << std::endl;
    }
}

void Player::restore() {
    if (!duck_) return;
    try {
        const Device box = device();
        // Speakers with coarse volume steps report a rounded value: a few percent off is still "unchanged".
        if (std::abs(box.volume - duck_->ducked_to) > 5) {
            std::cout << "[spotify] volume changed to " << box.volume << " while ducked, leaving it" << std::endl;
        } else {
            expect_ok(api("PUT", "/me/player/volume?volume_percent=" + std::to_string(duck_->restore_to) +
                                     "&device_id=" + box.id),
                      "restore");
        }
        duck_.reset();
    } catch (const std::exception& e) {
        std::cout << "[spotify] restore: " << e.what() << std::endl;
        if (++duck_->attempts >= 3) {
            std::cout << "[spotify] giving up restoring the volume to " << duck_->restore_to << std::endl;
            duck_.reset();
        }
    }
    save_duck();
}

void Player::recover() {
    if (cfg_.duck_file.empty()) return;
    std::ifstream f(cfg_.duck_file);
    if (!f) return;
    const auto j = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_object() && j["restore_to"].is_number_integer() && j["ducked_to"].is_number_integer()) {
        std::cout << "[spotify] restoring the volume a restart left ducked" << std::endl;
        duck_ = Duck{.restore_to = j["restore_to"], .ducked_to = j["ducked_to"]};
        restore();
    }
    std::remove(cfg_.duck_file.c_str());
    duck_.reset();
}

// A file in tmpfs ($XDG_RUNTIME_DIR): survives a service restart, not a reboot, and never touches the microSD.
void Player::save_duck() const {
    if (cfg_.duck_file.empty()) return;
    if (!duck_) {
        std::remove(cfg_.duck_file.c_str());
        return;
    }
    std::ofstream(cfg_.duck_file) << json{{"restore_to", duck_->restore_to}, {"ducked_to", duck_->ducked_to}}.dump();
}

void Player::warm_up() {
    try {
        warm_device_ = device();
        warmed_at_ = std::chrono::steady_clock::now();
    } catch (const std::exception& e) {
        warm_device_.reset();
        std::cout << "[spotify] warm-up: " << e.what() << std::endl;
    }
}

Result Player::dispatch(const std::string& action, const std::string& query) {
    if (action == "set_volume") return set_volume(query);
    if (action == "now_playing") return now_playing();
    // A warmed-up device is used once: its volume and "active" flag are only seconds old.
    const bool warm = warm_device_ && std::chrono::steady_clock::now() - warmed_at_ < std::chrono::seconds(15);
    const std::optional<Device> cached = std::exchange(warm_device_, std::nullopt);
    if (!warm) return dispatch(device(), action, query);
    try {
        return dispatch(*cached, action, query);
    } catch (const Failure& e) {
        if (e.status != 404) throw;  // 404 = device id gone (box restarted since the warm-up): look it up again
        std::cout << "[spotify] " << action << ": " << e.what() << ", retrying with a fresh device lookup" << std::endl;
        return dispatch(device(), action, query);
    }
}

Result Player::dispatch(const Device& d, const std::string& action, const std::string& query) {
    const std::string on = "?device_id=" + d.id;

    if (action == "play") return play(d, query);
    if (action == "play_playlist") return play_playlist(d, query);
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
        if (duck_) {  // "louder" over ducked music means louder than before the duck: applied by restore()
            duck_->restore_to = std::clamp(duck_->restore_to + step, 0, 100);
            save_duck();
            return volume_set(duck_->restore_to);
        }
        const int volume = std::clamp(d.volume + step, 0, 100);
        expect_ok(api("PUT", "/me/player/volume?volume_percent=" + std::to_string(volume) + "&device_id=" + d.id),
                  "volume");
        return volume_set(volume);
    }
    if (action == "shuffle") {
        const bool on = query != "off";
        expect_ok(api("PUT", std::string("/me/player/shuffle?state=") + (on ? "true" : "false") + "&device_id=" + d.id),
                  "shuffle");
        return said(on ? "Shuffle is on." : "Shuffle is off.");
    }
    if (action == "repeat") {
        const std::string state = query == "track" || query == "off" ? query : "context";
        expect_ok(api("PUT", "/me/player/repeat?state=" + state + "&device_id=" + d.id), "repeat");
        return said(state == "track" ? "Repeating this song." : state == "off" ? "Repeat is off." : "Repeat is on.");
    }
    if (action == "play_liked") return play_liked(d, query == "shuffle");
    throw Failure("unknown action \"" + action + "\"", "I can't do that with Spotify.");
}

Result Player::play(const Device& d, const std::string& query) {
    const std::string on = "?device_id=" + d.id;

    if (!query.empty()) {
        // "… playlist" searches playlists only. "<song> by <artist>" searches with field filters, which
        // matches far better than the plain text. Otherwise an exact artist name wins, then the best track.
        std::string q = lower(query);
        const bool want_playlist = q.size() > 9 && q.ends_with(" playlist");
        if (want_playlist) q.resize(q.size() - 9);
        // "play the ones" when "The Ones" is your playlist: yours wins over a search (exact or contained name)
        if (!want_playlist) {
            if (const json own = best_own_playlist(q, 500); !own.is_null()) {
                expect_ok(api("PUT", "/me/player/play" + on, {{"context_uri", own["uri"]}}), "play");
                return playing(true, "Playing your playlist " + own["name"].get<std::string>() + ".");
            }
        }
        std::string title = q;  // the name a track result should have
        std::string search = q;
        std::string types = want_playlist ? "playlist" : "artist,track,playlist";
        if (const auto by = q.rfind(" by "); !want_playlist && by != std::string::npos && by > 0) {
            title = q.substr(0, by);
            search = "track:" + title + " artist:" + q.substr(by + 4);
            types = "track";
        }
        auto search_for = [&](const std::string& text, const std::string& type) {
            const auto r = api("GET", "/search?q=" + url_encode(text) + "&type=" + type + "&limit=5");
            expect_ok(r, "search");
            return json::parse(r.body);
        };
        json results = search_for(search, types);
        if (types == "track" && best_item(results, "tracks", title).is_null()) {
            // Filters too strict (e.g. misheard artist): fall back to the plain text.
            std::cout << "[spotify] no result for " << search << ", retrying as plain text" << std::endl;
            results = search_for(q, "artist,track,playlist");
        }

        json body;
        std::string say;
        const json artist = best_item(results, "artists", q);
        const json track = best_item(results, "tracks", title);
        const json playlist = best_item(results, "playlists", q);
        if (!artist.is_null() && !want_playlist && core_name(artist["name"]) == core_name(q)) {
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

    // Resume. If the box isn't the active device yet, transfer playback to it (continues where the
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

Result Player::play_playlist(const Device& d, const std::string& name) {
    if (name.empty()) {
        if (cfg_.default_uri.empty())
            throw Failure("\"play my playlist\" without a name, but BUDDY_SPOTIFY_DEFAULT_URI is not set",
                          "Which playlist? You haven't set a default one.");
        expect_ok(api("PUT", "/me/player/play?device_id=" + d.id, {{"context_uri", cfg_.default_uri}}), "play");
        return playing(true);
    }

    const json best = best_own_playlist(name, 1);
    if (best.is_null()) {  // not one of yours: a public playlist of that name
        std::cout << "[spotify] no own playlist matches \"" << name << "\", searching public ones" << std::endl;
        return play(d, name + " playlist");
    }
    expect_ok(api("PUT", "/me/player/play?device_id=" + d.id, {{"context_uri", best["uri"]}}), "play");
    return playing(true, "Playing your playlist " + best["name"].get<std::string>() + ".");
}

// All playlists you own or follow, 50 per page; 200 is plenty for voice selection. Cached for 10 minutes: plain
// "play X" checks them on every command.
const json& Player::own_playlists() {
    const auto now = std::chrono::steady_clock::now();
    if (!playlists_.is_null() && now - playlists_at_ < std::chrono::minutes(10)) return playlists_;
    json all = json::array();
    for (int offset = 0; offset < 200; offset += 50) {
        const auto r = api("GET", "/me/playlists?limit=50&offset=" + std::to_string(offset));
        if (r.status == 401 || r.status == 403)  // login from before playlist scopes were requested
            throw Failure("playlists: HTTP " + std::to_string(r.status) + " " + error_reason(r) +
                              " (run buddy-spotify --login again for the playlist permission)",
                          "I'm not allowed to read your playlists yet. Please log in to Spotify again.");
        expect_ok(r, "playlists");
        const json page = json::parse(r.body);
        for (const auto& p : page["items"])
            if (p.is_object() && p["name"].is_string() && p["uri"].is_string()) all.push_back(p);
        if (page["items"].size() < 50) break;
    }
    playlists_ = std::move(all);
    playlists_at_ = now;
    return playlists_;
}

// Best-matching own playlist with at least min_score (see match_score), else null.
json Player::best_own_playlist(const std::string& name, int min_score) {
    json best = nullptr;
    int best_score = min_score - 1;
    for (const auto& p : own_playlists()) {
        const int score = match_score(p["name"], name);
        if (score > best_score) {
            best_score = score;
            best = p;
        }
    }
    return best;
}

Result Player::set_volume(const std::string& query) {
    int volume = -1;
    try {
        std::size_t used = 0;
        volume = std::stoi(query, &used);
        if (used != query.size()) volume = -1;
    } catch (const std::exception&) {
    }
    if (volume < 0 || volume > 100)
        throw Failure("volume \"" + query + "\" out of range", "The volume goes from 0 to 100.");
    if (duck_) {  // applied by restore(), like "louder"
        duck_->restore_to = volume;
        save_duck();
        return volume_set(volume);
    }
    const Device d = device();
    expect_ok(api("PUT", "/me/player/volume?volume_percent=" + std::to_string(volume) + "&device_id=" + d.id), "volume");
    return volume_set(volume);
}

Result Player::now_playing() {
    const auto r = api("GET", "/me/player");
    if (r.status != 204) expect_ok(r, "player");
    const json p = r.status == 204 ? json::object() : json::parse(r.body);
    const json item = p.value("item", json());
    if (!item.is_object() || !item["name"].is_string()) return said("Nothing is playing.");
    std::string say = "This is " + item["name"].get<std::string>();
    if (item.contains("artists") && !item["artists"].empty() && item["artists"][0]["name"].is_string())
        say += " by " + item["artists"][0]["name"].get<std::string>();
    else if (item.contains("show") && item["show"]["name"].is_string())  // podcast episode
        say += " from " + item["show"]["name"].get<std::string>();
    return said(say + ".");
}

// Liked songs aren't an official play context (only albums, artists and playlists are). The undocumented
// "spotify:user:<id>:collection" plays the whole library ⚠️; if Spotify refuses it, the 50 newest liked songs
// are played as a track list (needs the user-library-read scope).
Result Player::play_liked(const Device& d, bool shuffle) {
    const auto me = api("GET", "/me");
    expect_ok(me, "me");
    const std::string user = json::parse(me.body).value("id", "");
    auto r = api("PUT", "/me/player/play?device_id=" + d.id, {{"context_uri", "spotify:user:" + user + ":collection"}});
    if (r.status / 100 != 2) {
        std::cout << "[spotify] collection context refused (" << r.status << " " << error_reason(r)
                  << "), playing the newest liked songs" << std::endl;
        const auto t = api("GET", "/me/tracks?limit=50");
        if (t.status == 401 || t.status == 403)
            throw Failure("liked songs: HTTP " + std::to_string(t.status) + " (run buddy-spotify --login again)",
                          "I'm not allowed to read your liked songs yet. Please log in to Spotify again.");
        expect_ok(t, "liked songs");
        json uris = json::array();
        for (const auto& i : json::parse(t.body)["items"])
            if (i.contains("track") && i["track"]["uri"].is_string()) uris.push_back(i["track"]["uri"]);
        if (uris.empty()) throw Failure("no liked songs", "You don't have any liked songs yet.");
        if (shuffle) std::shuffle(uris.begin(), uris.end(), std::mt19937(std::random_device{}()));
        r = api("PUT", "/me/player/play?device_id=" + d.id, {{"uris", uris}});
    }
    expect_ok(r, "play liked");
    if (shuffle) expect_ok(api("PUT", "/me/player/shuffle?state=true&device_id=" + d.id), "shuffle");
    return playing(true, shuffle ? "Shuffling your liked songs." : "Playing your liked songs.");
}

std::vector<std::string> Player::devices() {
    const auto r = api("GET", "/me/player/devices");
    expect_ok(r, "devices");
    std::vector<std::string> out;
    for (const auto& d : json::parse(r.body)["devices"]) {
        std::string line = "\"" + d.value("name", "?") + "\" (" + d.value("type", "?");
        if (d.value("is_active", false)) line += ", active";
        if (d["volume_percent"].is_number()) line += ", volume " + std::to_string(d["volume_percent"].get<int>()) + " %";
        out.push_back(line + ")");
    }
    return out;
}

Player::Device Player::device() {
    const auto r = api("GET", "/me/player/devices");
    expect_ok(r, "devices");
    const json devices = json::parse(r.body);
    for (const auto& d : devices["devices"]) {
        if (lower(d.value("name", "")) != lower(cfg_.device_name) || !d["id"].is_string()) continue;
        last_id_ = d["id"];
        return {.id = d["id"], .active = d.value("is_active", false),
                .volume = d["volume_percent"].is_number() ? d["volume_percent"].get<int>() : 50};
    }
    // Some Connect speakers leave the list in standby but still accept playback on their old id ⚠️
    if (!last_id_.empty()) {
        std::cout << "[spotify] " << cfg_.device_name << " not in the device list, trying its last id" << std::endl;
        return {.id = last_id_};
    }
    throw Failure("device \"" + cfg_.device_name + "\" not found (off, or not on the network?)",
                  cfg_.device_name + " isn't available.");
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
    if (reason == "PREMIUM_REQUIRED") throw Failure(detail, "Spotify Premium is required for that.", r.status);
    if (r.status == 429) throw Failure(detail, "Spotify is busy, try again in a minute.", r.status);
    if (r.status == 404) throw Failure(detail, "There's nothing to play on " + cfg_.device_name + ".", r.status);
    throw Failure(detail, "Spotify didn't accept that.", r.status);
}

} // namespace buddy::spotify
