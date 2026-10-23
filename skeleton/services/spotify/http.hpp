#pragma once
// Minimal blocking HTTPS client on libcurl. One instance per thread; it reuses the connection
// (keep-alive), so the second call to api.spotify.com skips the TLS handshake.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace buddy::spotify {

struct HttpResponse {
    long status = 0;
    std::string body;
};

// Transport failure (DNS, timeout, TLS). HTTP error statuses are not exceptions.
struct HttpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Http {
public:
    Http();
    ~Http();
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    // method: GET, POST, PUT. For POST/PUT the body is always sent (Spotify wants Content-Length: 0
    // on empty PUTs).
    HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body = "");

private:
    void* curl_;  // CURL*
};

std::string url_encode(std::string_view s);

} // namespace buddy::spotify
