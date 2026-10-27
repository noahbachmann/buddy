#pragma once
// Http for the tests: canned responses per "METHOD /path" (query string ignored), every request recorded.

#include <gtest/gtest.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "http.hpp"

namespace buddy::spotify::test {

struct FakeHttp : Http {
    struct Call {
        std::string method, url, body;
    };
    std::vector<Call> calls;

    // Responses for `route` ("PUT /me/player/volume", "POST token") in order; once used up, the last one repeats.
    void on(const std::string& route, long status, std::string body = "") {
        routes_[route].queue.push_back({status, std::move(body)});
    }

    HttpResponse request(const std::string& method, const std::string& url, const std::vector<std::string>&,
                         const std::string& body) override {
        calls.push_back({method, url, body});
        auto it = routes_.find(method + " " + path(url));
        if (it == routes_.end()) {
            ADD_FAILURE() << "unexpected " << method << " " << url;
            return {599, ""};
        }
        Route& r = it->second;
        if (!r.queue.empty()) {
            r.last = std::move(r.queue.front());
            r.queue.pop_front();
        }
        return r.last;
    }

    // Requests to `route` so far, e.g. count("GET /me/player/devices").
    std::vector<Call> to(const std::string& route) const {
        std::vector<Call> out;
        for (const auto& c : calls)
            if (c.method + " " + path(c.url) == route) out.push_back(c);
        return out;
    }

private:
    static std::string path(const std::string& url) {
        if (url == "https://accounts.spotify.com/api/token") return "token";
        const std::string api = "https://api.spotify.com/v1";
        std::string p = url.starts_with(api) ? url.substr(api.size()) : url;
        return p.substr(0, p.find('?'));
    }

    struct Route {
        std::deque<HttpResponse> queue;
        HttpResponse last;
    };
    std::map<std::string, Route> routes_;
};

// Fresh directory per test for token and duck files.
inline std::filesystem::path temp_dir() {
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    auto dir = std::filesystem::temp_directory_path() /
               ("buddy-test-" + std::string(info->test_suite_name()) + "-" + info->name());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline void write(const std::filesystem::path& p, const std::string& text) { std::ofstream(p) << text; }

inline std::string read(const std::filesystem::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace buddy::spotify::test
