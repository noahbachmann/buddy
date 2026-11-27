#include <gtest/gtest.h>

#include <filesystem>

#include "bridge.hpp"
#include "fake_http.hpp"

using namespace buddy::hue;
using namespace buddy::test;
using nlohmann::json;

namespace {

const BridgeConfig kBridge{.id = "ecb5fafffe9164da", .address = "192.168.1.219", .app_key = "KEY"};

} // namespace

TEST(Bridge, UrlsAndTls) {
    FakeHttp http;
    const Bridge bridge(kBridge, http);
    EXPECT_EQ(bridge.url("/clip/v2/resource"), "https://ecb5fafffe9164da/clip/v2/resource");
    EXPECT_EQ(bridge.headers()[0], "hue-application-key: KEY");
    const buddy::CurlOptions o = Bridge::curl_options(kBridge);
    EXPECT_EQ(o.resolve, std::vector<std::string>{"ecb5fafffe9164da:443:192.168.1.219"});
    EXPECT_TRUE(o.ca_pem.starts_with("-----BEGIN CERTIFICATE-----"));
}

TEST(Bridge, Resources) {
    FakeHttp http;
    Bridge bridge(kBridge, http);
    http.on("GET /clip/v2/resource", 200, R"({"errors": [], "data": [{"id": "a", "type": "light"}]})");
    EXPECT_EQ(bridge.resources().size(), 1u);
    http.on("GET /clip/v2/resource", 403, R"({"errors": [{"description": "unauthorized user"}], "data": []})");
    try {
        bridge.resources();
        FAIL();
    } catch (const BridgeError& e) {
        EXPECT_EQ(e.status, 403);
        EXPECT_NE(std::string(e.what()).find("unauthorized user"), std::string::npos);
    }
}

TEST(Bridge, Put) {
    FakeHttp http;
    Bridge bridge(kBridge, http);
    http.on("PUT /clip/v2/resource/light/l1", 200, R"({"errors": [], "data": [{"rid": "l1", "rtype": "light"}]})");
    bridge.send({.type = "light", .id = "l1", .body = {{"on", {{"on", true}}}}});
    EXPECT_EQ(http.calls.back().body, R"({"on":{"on":true}})");
    // 207: partly applied, still an error to report
    http.on("PUT /clip/v2/resource/light/l1", 207, R"({"errors": [{"description": "device unreachable"}], "data": []})");
    EXPECT_THROW(bridge.send({.type = "light", .id = "l1", .body = json::object()}), BridgeError);
}

TEST(Bridge, PostAndDelete) {
    FakeHttp http;
    Bridge bridge(kBridge, http);
    http.on("POST /clip/v2/resource/scene", 200, R"({"errors": [], "data": [{"rid": "new", "rtype": "scene"}]})");
    bridge.send({.type = "scene", .id = "", .body = {{"type", "scene"}}, .method = "POST"});
    EXPECT_EQ(http.calls.back().body, R"({"type":"scene"})");
    http.on("DELETE /clip/v2/resource/scene/s1", 200, R"({"errors": [], "data": [{"rid": "s1", "rtype": "scene"}]})");
    bridge.send({.type = "scene", .id = "s1", .body = nullptr, .method = "DELETE"});
    EXPECT_EQ(http.calls.back().body, "");
}

TEST(Bridge, Pair) {
    FakeHttp http;
    Bridge bridge({.id = kBridge.id, .address = kBridge.address, .app_key = ""}, http);
    http.on("POST /api", 200, R"([{"error": {"type": 101, "address": "", "description": "link button not pressed"}}])");
    http.on("POST /api", 200, R"([{"success": {"username": "NEWKEY", "clientkey": "CK"}}])");
    EXPECT_EQ(bridge.pair("buddy#pi"), std::nullopt);
    EXPECT_EQ(bridge.pair("buddy#pi"), "NEWKEY");
    EXPECT_EQ(json::parse(http.calls[0].body)["generateclientkey"], true);
    http.on("POST /api", 200, R"([{"error": {"type": 7, "description": "invalid value"}}])");
    EXPECT_THROW(bridge.pair("buddy#pi"), BridgeError);
}

TEST(Bridge, Discovery) {
    FakeHttp http;
    http.on("GET /", 200, R"([{"id": "ECB5FAFFFE9164DA", "internalipaddress": "192.168.1.219", "port": 443}])");
    const auto found = discover(http);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].id, "ecb5fafffe9164da");
    EXPECT_EQ(found[0].address, "192.168.1.219");
    http.on("GET /api/config", 200, R"({"name": "Hue Bridge", "bridgeid": "ECB5FAFFFE9164DA"})");
    EXPECT_EQ(bridge_id_at(http, "192.168.1.219"), "ecb5fafffe9164da");
    EXPECT_EQ(http.calls.back().url, "http://192.168.1.219/api/config");
}

TEST(Bridge, KeyFile) {
    const auto file = temp_dir() / "config" / "hue.json";
    EXPECT_EQ(load_key_file(file), std::nullopt);
    save_key_file(file, {.id = "ECB5", .address = "10.0.0.2", .app_key = "K"});
    const auto loaded = load_key_file(file);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->id, "ecb5");
    EXPECT_EQ(loaded->app_key, "K");
    const auto perms = std::filesystem::status(file).permissions();
    EXPECT_EQ(perms & std::filesystem::perms::all, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    write(file, R"({"bridge_id": "x"})");  // incomplete
    EXPECT_EQ(load_key_file(file), std::nullopt);
}
