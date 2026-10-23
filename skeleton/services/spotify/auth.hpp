#pragma once
// Spotify login: OAuth 2.0 Authorization Code with PKCE (no client secret on the Pi).
//
// Once:   buddy-spotify --login  → prints a URL; you log in on the PC's browser; the browser is redirected
//         to http://127.0.0.1:8888/callback?code=… which fails to load (nothing listens there): paste that
//         URL back into the terminal. The refresh token is saved to the token file (mode 600).
// Always: access tokens (valid 1 h) are fetched with the refresh token when needed. Spotify may rotate
//         the refresh token on each refresh; the new one is saved.

#include <chrono>
#include <iosfwd>
#include <stdexcept>
#include <string>

#include "http.hpp"

namespace buddy::spotify {

struct AuthConfig {
    std::string client_id;     // BUDDY_SPOTIFY_CLIENT_ID, from the Spotify developer dashboard
    std::string redirect_uri;  // BUDDY_SPOTIFY_REDIRECT_URI, must match the dashboard exactly
    std::string token_file;    // BUDDY_SPOTIFY_TOKEN_FILE
};

// Not logged in, or the refresh token was revoked: run --login again.
struct AuthError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Auth {
public:
    Auth(AuthConfig cfg, Http& http);

    bool logged_in() const { return !refresh_token_.empty(); }

    // Interactive login on a terminal. Throws on failure.
    void login(std::istream& in, std::ostream& out);

    // Valid access token, refreshed if it expires within a minute. Throws AuthError or HttpError.
    std::string access_token();

    // After a 401: forget the access token so the next call refreshes.
    void invalidate() { access_token_.clear(); }

private:
    void token_request(const std::string& form);
    void save() const;

    AuthConfig cfg_;
    Http& http_;
    std::string refresh_token_;
    std::string access_token_;
    std::chrono::steady_clock::time_point expires_{};
};

} // namespace buddy::spotify
