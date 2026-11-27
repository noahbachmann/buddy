#include "buddy/curl_http.hpp"

#include <curl/curl.h>

namespace buddy {

namespace {

size_t append(char* data, size_t size, size_t count, void* out) {
    static_cast<std::string*>(out)->append(data, size * count);
    return size * count;
}

struct StreamState {
    const std::function<void(std::string_view)>& on_data;
    const std::function<bool()>& keep_going;
};

size_t deliver(char* data, size_t size, size_t count, void* state) {
    static_cast<StreamState*>(state)->on_data({data, size * count});
    return size * count;
}

int progress(void* state, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<StreamState*>(state)->keep_going() ? 0 : 1;  // non-zero aborts
}

} // namespace

CurlHttp::CurlHttp(CurlOptions options) : curl_(curl_easy_init()), options_(std::move(options)), resolve_(nullptr) {
    if (!curl_) throw HttpError("curl_easy_init failed");
    curl_slist* list = nullptr;
    for (const auto& r : options_.resolve) list = curl_slist_append(list, r.c_str());
    resolve_ = list;
}

CurlHttp::~CurlHttp() {
    curl_easy_cleanup(curl_);
    curl_slist_free_all(static_cast<curl_slist*>(resolve_));
}

void* CurlHttp::prepare(const std::string& url, const std::vector<std::string>& headers) {
    curl_easy_reset(curl_);  // keeps the connection cache
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    if (!options_.ca_pem.empty()) {
        curl_blob blob{options_.ca_pem.data(), options_.ca_pem.size(), CURL_BLOB_NOCOPY};  // options_ outlives the handle's use
        curl_easy_setopt(curl_, CURLOPT_CAINFO_BLOB, &blob);
    }
    if (resolve_) curl_easy_setopt(curl_, CURLOPT_RESOLVE, resolve_);

    curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, list);
    return list;
}

HttpResponse CurlHttp::request(const std::string& method, const std::string& url,
                               const std::vector<std::string>& headers, const std::string& body) {
    auto* list = static_cast<curl_slist*>(prepare(url, headers));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, options_.timeout_s);
    if (method != "GET") {
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    HttpResponse response;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

    const CURLcode rc = curl_easy_perform(curl_);
    curl_slist_free_all(list);
    if (rc != CURLE_OK) throw HttpError(method + " " + url + ": " + curl_easy_strerror(rc));
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

long CurlHttp::stream(const std::string& url, const std::vector<std::string>& headers,
                      const std::function<void(std::string_view)>& on_data, const std::function<bool()>& keep_going) {
    auto* list = static_cast<curl_slist*>(prepare(url, headers));
    StreamState state{on_data, keep_going};
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, deliver);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &state);
    // quiet streams are normal: find dead peers by keepalive probes, not by a timeout
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPINTVL, 10L);

    const CURLcode rc = curl_easy_perform(curl_);
    curl_slist_free_all(list);
    long status = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK && rc != CURLE_ABORTED_BY_CALLBACK) throw HttpError("GET " + url + ": " + curl_easy_strerror(rc));
    return status;
}

} // namespace buddy
