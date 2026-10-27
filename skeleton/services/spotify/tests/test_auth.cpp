#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "fake_http.hpp"

using namespace buddy::spotify;
using namespace buddy::spotify::test;

namespace {

struct AuthTest : testing::Test {
    std::filesystem::path token_file = temp_dir() / "token.json";
    FakeHttp http;
    Auth make() { return Auth({.client_id = "cid", .redirect_uri = "", .token_file = token_file}, http); }
};

} // namespace

TEST_F(AuthTest, NoTokenFile) {
    Auth auth = make();
    EXPECT_FALSE(auth.logged_in());
    EXPECT_THROW(auth.access_token(), AuthError);
    EXPECT_TRUE(http.calls.empty());
}

TEST_F(AuthTest, BrokenTokenFilesDontCrash) {  // 45a6825
    for (const char* text : {"{}", "{\"refresh_token\": 5}", "not json", ""}) {
        write(token_file, text);
        EXPECT_FALSE(make().logged_in()) << text;
    }
}

TEST_F(AuthTest, RefreshesOnceAndCaches) {
    write(token_file, R"({"refresh_token": "R1"})");
    http.on("POST token", 200, R"({"access_token": "A1", "expires_in": 3600})");
    Auth auth = make();
    EXPECT_EQ(auth.access_token(), "A1");
    EXPECT_EQ(auth.access_token(), "A1");
    ASSERT_EQ(http.calls.size(), 1u);
    EXPECT_NE(http.calls[0].body.find("grant_type=refresh_token&refresh_token=R1"), std::string::npos);

    auth.invalidate();  // after a 401
    auth.access_token();
    EXPECT_EQ(http.calls.size(), 2u);
}

TEST_F(AuthTest, ExpiringTokenIsRefreshed) {
    write(token_file, R"({"refresh_token": "R1"})");
    http.on("POST token", 200, R"({"access_token": "A1", "expires_in": 30})");  // inside the 60 s margin
    Auth auth = make();
    auth.access_token();
    auth.access_token();
    EXPECT_EQ(http.calls.size(), 2u);
}

TEST_F(AuthTest, RotatedRefreshTokenIsSaved) {
    write(token_file, R"({"refresh_token": "R1"})");
    http.on("POST token", 200, R"({"access_token": "A1", "refresh_token": "R2"})");
    make().access_token();
    EXPECT_EQ(nlohmann::json::parse(read(token_file))["refresh_token"], "R2");
    EXPECT_FALSE(std::filesystem::exists(token_file.string() + ".tmp"));
}

TEST_F(AuthTest, RevokedRefreshTokenIsAnAuthError) {
    write(token_file, R"({"refresh_token": "R1"})");
    http.on("POST token", 400, R"({"error": "invalid_grant"})");
    EXPECT_THROW(make().access_token(), AuthError);
}
