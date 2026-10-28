#include <gtest/gtest.h>

#include "fake_spotify.hpp"
#include "player.hpp"

using namespace buddy::spotify;
using namespace buddy::spotify::test;
using nlohmann::json;

namespace {

json device(const std::string& id, const std::string& name, bool active = false, int volume = 50) {
    return {{"id", id}, {"name", name}, {"is_active", active}, {"volume_percent", volume}, {"type", "Speaker"}};
}

struct PlayerTest : testing::Test {
    std::filesystem::path dir = temp_dir();
    FakeSpotify http;
    std::optional<Auth> auth;
    std::optional<Player> player;

    PlayerTest() {
        write(dir / "token.json", R"({"refresh_token": "R"})");
        http.on("POST token", 200, R"({"access_token": "A"})");
        auth.emplace(AuthConfig{.client_id = "cid", .redirect_uri = "", .token_file = dir / "token.json"}, http);
        player.emplace(PlayerConfig{.device_name = "Main speaker", .volume_step = 10, .default_uri = "", .duck_percent = 30,
                                    .duck_file = dir / "duck.json", .duck_devices = {"Kitchen Echo"}},
                       *auth, http);
    }

    // Queues one device-list response (repeats until the next one is queued).
    void devices(const std::vector<json>& list) { http.on("GET /me/player/devices", 200, json{{"devices", list}}.dump()); }
    // What GET /me/player says is playing.
    void now(const json& d, bool playing = true) {
        http.on("GET /me/player", 200, json{{"is_playing", playing}, {"device", d}}.dump());
    }
    std::string last_url(const std::string& route) {
        const auto calls = http.to(route);
        return calls.empty() ? "" : calls.back().url;
    }
    std::string last_body(const std::string& route) {
        const auto calls = http.to(route);
        return calls.empty() ? "" : calls.back().body;
    }
    Result run(Command c) { return player->run(c); }
};

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

// --- which device ---

TEST_F(PlayerTest, NamedSpeakerMatchesFuzzily) {
    devices({device("box", "Main speaker"), device("bath", "Bathroom Speaker"), device("px", "Pixel 8")});
    http.on("PUT /me/player", 204);
    const std::vector<std::pair<std::string, std::string>> spoken = {
        {"bathroom", "bath"}, {"the bathroom speakers", "bath"}, {"Bathroom Speaker", "bath"},
        {"my pixel eight", "px"}, {"the box", "box"}, {"music box", "box"}};
    for (const auto& [name, id] : spoken) {
        const Result r = run({.action = "play", .device = name});
        EXPECT_TRUE(r.ok) << name << ": " << r.say;
        EXPECT_EQ(json::parse(last_body("PUT /me/player"))["device_ids"][0], id) << name;
    }
}

TEST_F(PlayerTest, NamesTheSpeakerUnlessItIsTheBox) {
    devices({device("box", "Main speaker"), device("bath", "Bathroom Speaker")});
    http.on("PUT /me/player", 204);
    EXPECT_EQ(run({.action = "play", .device = "bathroom"}).say, "Moving the music to Bathroom Speaker.");
    EXPECT_EQ(run({.action = "play", .device = "the box"}).say, "");
}

TEST_F(PlayerTest, UnknownSpeaker) {
    devices({device("box", "Main speaker"), device("bath", "Bathroom Speaker")});
    const Result r = run({.action = "play", .device = "garage"});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.say, "I can't find a speaker called garage.");
}

TEST_F(PlayerTest, UnknownSpeakerRunsTheFallbackOnTheBox) {
    devices({device("box", "Main speaker")});
    http.on("PUT /me/player", 204);
    EXPECT_TRUE(run({.action = "play", .device = "garage", .fallback_action = "play"}).ok);
    EXPECT_EQ(json::parse(last_body("PUT /me/player"))["device_ids"][0], "box");
}

TEST_F(PlayerTest, AmbiguousSpeakerAsks) {
    devices({device("box", "Main speaker"), device("k1", "Kitchen Echo"), device("k2", "Kitchen Speaker")});
    EXPECT_EQ(run({.action = "play", .device = "kitchen"}).say, "Which speaker, Kitchen Echo or Kitchen Speaker?");
}

TEST_F(PlayerTest, PlaybackGoesToTheBoxControlsToTheActiveDevice) {
    devices({device("box", "Main speaker"), device("px", "Pixel 8", /*active=*/true)});
    http.on("PUT /me/player/pause", 204);
    http.on("PUT /me/player", 204);
    run({.action = "pause"});
    EXPECT_TRUE(has(last_url("PUT /me/player/pause"), "device_id=px"));
    run({.action = "play"});
    EXPECT_EQ(json::parse(last_body("PUT /me/player"))["device_ids"][0], "box");
}

TEST_F(PlayerTest, BoxInStandbyKeepsItsLastId) {
    devices({device("box", "Main speaker")});
    devices({device("px", "Pixel 8")});
    http.on("PUT /me/player", 204);
    run({.action = "play"});
    EXPECT_TRUE(run({.action = "play"}).ok);
    EXPECT_EQ(json::parse(last_body("PUT /me/player"))["device_ids"][0], "box");
}

TEST_F(PlayerTest, BoxNeverSeen) {
    devices({device("px", "Pixel 8")});
    EXPECT_EQ(run({.action = "play"}).say, "Main speaker isn't available.");
}

// --- what plays ---

TEST_F(PlayerTest, OwnPlaylistWinsOverSearch) {
    devices({device("box", "Main speaker", true)});
    http.on("GET /me/playlists", 200, R"({"items": [{"name": "The Ones", "uri": "spotify:playlist:ones"}]})");
    http.on("PUT /me/player/play", 204);
    EXPECT_EQ(run({.action = "play", .query = "the ones"}).say, "Playing your playlist The Ones.");
    EXPECT_EQ(json::parse(last_body("PUT /me/player/play"))["context_uri"], "spotify:playlist:ones");
    EXPECT_TRUE(http.to("GET /search").empty());
}

TEST_F(PlayerTest, SongByArtistSearchesWithFilters) {
    devices({device("box", "Main speaker", true)});
    http.on("GET /me/playlists", 200, R"({"items": []})");
    http.on("GET /search", 200, R"({"tracks": {"items": [
        {"name": "One More Time", "uri": "spotify:track:omt", "artists": [{"name": "Daft Punk"}]}]}})");
    http.on("PUT /me/player/play", 204);
    EXPECT_EQ(run({.action = "play", .query = "one more time by daft punk"}).say, "Playing One More Time by Daft Punk.");
    EXPECT_TRUE(has(last_url("GET /search"), "q=track%3Aone%20more%20time%20artist%3Adaft%20punk&type=track"));
    EXPECT_EQ(json::parse(last_body("PUT /me/player/play"))["uris"][0], "spotify:track:omt");
}

TEST_F(PlayerTest, LikedSongsFallBackToTheNewestTracks) {
    devices({device("box", "Main speaker", true)});
    http.on("GET /me", 200, R"({"id": "u"})");
    http.on("PUT /me/player/play", 403, R"({"error": {"reason": "UNKNOWN"}})");  // collection context refused
    http.on("PUT /me/player/play", 204);
    http.on("GET /me/tracks", 200, R"({"items": [{"track": {"uri": "t1"}}, {"track": null}, {}, {"track": {"uri": "t2"}}]})");
    const Result r = run({.action = "play_liked"});
    EXPECT_TRUE(r.ok) << r.say;
    EXPECT_EQ(json::parse(last_body("PUT /me/player/play"))["uris"], json({"t1", "t2"}));
}

// --- token ---

TEST_F(PlayerTest, ExpiredTokenIsRefreshedOnceAndRetried) {
    devices({device("box", "Main speaker", true)});
    http.on("PUT /me/player/pause", 401);
    http.on("PUT /me/player/pause", 204);
    EXPECT_TRUE(run({.action = "pause"}).ok);
    EXPECT_EQ(http.to("POST token").size(), 2u);
    EXPECT_EQ(http.to("PUT /me/player/pause").size(), 2u);
}

// --- ducking ---

TEST_F(PlayerTest, DuckVolumeCommandsAndRestore) {
    now(device("box", "Main speaker", true, 60));
    devices({device("box", "Main speaker", true, 18)});
    http.on("PUT /me/player/volume", 204);

    player->duck();
    EXPECT_TRUE(has(last_url("PUT /me/player/volume"), "volume_percent=18&device_id=box"));
    EXPECT_TRUE(player->ducked());
    EXPECT_EQ(json::parse(read(dir / "duck.json"))["restore_to"], 60);

    // "louder" while ducked changes what restore() brings back, not the ducked volume
    EXPECT_EQ(run({.action = "volume_up"}).volume, 70);
    EXPECT_EQ(http.to("PUT /me/player/volume").size(), 1u);

    player->restore();
    EXPECT_TRUE(has(last_url("PUT /me/player/volume"), "volume_percent=70&device_id=box"));
    EXPECT_FALSE(player->ducked());
    EXPECT_FALSE(std::filesystem::exists(dir / "duck.json"));
}

TEST_F(PlayerTest, RestoreLeavesAVolumeChangedElsewhere) {
    now(device("box", "Main speaker", true, 60));
    devices({device("box", "Main speaker", true, 40)});  // someone used the phone app meanwhile
    http.on("PUT /me/player/volume", 204);
    player->duck();
    player->restore();
    EXPECT_EQ(http.to("PUT /me/player/volume").size(), 1u);
    EXPECT_FALSE(player->ducked());
}

TEST_F(PlayerTest, OnlyDucksDevicesInTheRoom) {
    devices({});
    now(device("bath", "Bathroom Speaker", true, 60));
    player->duck();
    EXPECT_FALSE(player->ducked());

    http.on("PUT /me/player/volume", 204);
    now(device("k", "Kitchen Echo", true, 60));  // in duck_devices
    player->duck();
    EXPECT_TRUE(player->ducked());
}

TEST_F(PlayerTest, RecoverRestoresALeftoverDuck) {
    write(dir / "duck.json", R"({"device_id": "box", "restore_to": 60, "ducked_to": 18})");
    devices({device("box", "Main speaker", true, 18)});
    http.on("PUT /me/player/volume", 204);
    player->recover();
    EXPECT_TRUE(has(last_url("PUT /me/player/volume"), "volume_percent=60&device_id=box"));
    EXPECT_FALSE(std::filesystem::exists(dir / "duck.json"));
}

TEST_F(PlayerTest, RecoverIgnoresBrokenFiles) {
    for (const char* text : {R"({"device_id": "box"})", "[1, 2]", "garbage", ""}) {
        write(dir / "duck.json", text);
        player->recover();
        EXPECT_FALSE(player->ducked()) << text;
        EXPECT_FALSE(std::filesystem::exists(dir / "duck.json")) << text;
    }
    EXPECT_TRUE(http.calls.empty());
}

TEST_F(PlayerTest, StaleWarmListIsRefetchedOn404) {
    http.on("GET /me/player", 204);  // nothing playing: duck() only warms up
    devices({device("box", "Main speaker", true)});
    http.on("PUT /me/player/play", 404, R"({"error": {"message": "Device not found"}})");
    http.on("PUT /me/player/play", 204);
    player->duck();
    EXPECT_TRUE(run({.action = "play"}).ok);
    EXPECT_EQ(http.to("GET /me/player/devices").size(), 2u);
}
