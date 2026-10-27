#pragma once
// Minimal blocking HTTPS client on libcurl. One instance per thread; it reuses the connection
// (keep-alive), so the second call to api.spotify.com skips the TLS handshake.

#include "http.hpp"

namespace buddy::spotify {

class CurlHttp final : public Http {
public:
    CurlHttp();
    ~CurlHttp() override;
    CurlHttp(const CurlHttp&) = delete;
    CurlHttp& operator=(const CurlHttp&) = delete;

    HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body = "") override;

private:
    void* curl_;  // CURL*
};

} // namespace buddy::spotify
