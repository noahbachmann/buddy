#pragma once
// Minimal blocking HTTPS client on libcurl. One instance per thread; it reuses the connection
// (keep-alive), so the second call to the same host skips the TLS handshake.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "buddy/http.hpp"

namespace buddy {

struct CurlOptions {
    std::string ca_pem;                // trust only these PEM certificates instead of the system's; empty = system
    std::vector<std::string> resolve;  // "host:port:address" (CURLOPT_RESOLVE): TLS checks `host`, connects to `address`
    long timeout_s = 10;               // whole request; stream() has none
};

class CurlHttp final : public Http {
public:
    explicit CurlHttp(CurlOptions options = {});
    ~CurlHttp() override;
    CurlHttp(const CurlHttp&) = delete;
    CurlHttp& operator=(const CurlHttp&) = delete;

    HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body = "") override;

    // Long-lived GET (server-sent events). on_data gets every chunk as it arrives; keep_going is polled about once
    // a second, false ends the stream. Dead peers are found by TCP keepalive. Returns the HTTP status when the
    // stream ends (also after keep_going said stop). Throws HttpError on transport failures.
    long stream(const std::string& url, const std::vector<std::string>& headers,
                const std::function<void(std::string_view)>& on_data, const std::function<bool()>& keep_going);

private:
    void* prepare(const std::string& url, const std::vector<std::string>& headers);  // → curl_slist* to free

    void* curl_;  // CURL*
    CurlOptions options_;
    void* resolve_;  // curl_slist*, lives as long as the handle uses it
};

} // namespace buddy
