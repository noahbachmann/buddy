#pragma once
// FakeHttp with Spotify's routes: "POST token", "GET /me/player/devices", …

#include "fake_http.hpp"

namespace buddy::spotify::test {

using namespace buddy::test;

struct FakeSpotify : FakeHttp {
    FakeSpotify() : FakeHttp({{"https://accounts.spotify.com/api/token", "token"}, {"https://api.spotify.com/v1", ""}}) {}
};

} // namespace buddy::spotify::test
