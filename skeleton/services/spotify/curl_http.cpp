#include "curl_http.hpp"

#include <curl/curl.h>

namespace buddy::spotify {

namespace {

size_t append(char* data, size_t size, size_t count, void* out) {
    static_cast<std::string*>(out)->append(data, size * count);
    return size * count;
}

} // namespace

CurlHttp::CurlHttp() : curl_(curl_easy_init()) {
    if (!curl_) throw HttpError("curl_easy_init failed");
}

CurlHttp::~CurlHttp() { curl_easy_cleanup(curl_); }

HttpResponse CurlHttp::request(const std::string& method, const std::string& url,
                               const std::vector<std::string>& headers, const std::string& body) {
    curl_easy_reset(curl_);  // keeps the connection cache
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    if (method != "GET") {
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, list);

    HttpResponse response;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

    const CURLcode rc = curl_easy_perform(curl_);
    curl_slist_free_all(list);
    if (rc != CURLE_OK) throw HttpError(method + " " + url + ": " + curl_easy_strerror(rc));
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

} // namespace buddy::spotify
