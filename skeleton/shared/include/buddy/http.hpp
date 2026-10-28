#pragma once
// HTTPS transport behind an interface: CurlHttp (curl_http.hpp) in the services, FakeHttp (shared/tests) in the tests.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace buddy {

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
    virtual ~Http() = default;

    // method: GET, POST, PUT. For POST/PUT the body is always sent (Spotify wants Content-Length: 0
    // on empty PUTs). Throws HttpError on transport failures.
    virtual HttpResponse request(const std::string& method, const std::string& url,
                                 const std::vector<std::string>& headers, const std::string& body = "") = 0;
};

std::string url_encode(std::string_view s);

} // namespace buddy
