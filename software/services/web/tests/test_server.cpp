// The routes on a real server (127.0.0.1, free port) with httplib's client; commands land in a vector.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "server.hpp"

using namespace buddy::web;
using nlohmann::json;

namespace {

struct ServerTest : testing::Test {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "buddy-web-test";
    StateStore store;
    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> published;
    httplib::Server server;
    std::thread thread;
    int port = 0;

    ServerTest() {
        std::filesystem::create_directories(root);
        std::ofstream(root / "index.html") << "<h1>Buddy</h1>";
        const bool ok = add_routes(
            server, store,
            [this](const std::string& t, const std::string& p) {
                std::lock_guard lock(mutex);
                published.emplace_back(t, p);
            },
            {.root = root.string(), .extra_hosts = {}, .keepalive = std::chrono::milliseconds(50)});
        EXPECT_TRUE(ok);
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~ServerTest() override {
        store.close();
        server.stop();
        thread.join();
    }
    httplib::Client client() { return httplib::Client("127.0.0.1", port); }
    httplib::Result post(const std::string& path, const std::string& body, httplib::Headers headers = {}) {
        auto c = client();
        return c.Post(path, headers, body, "application/json");
    }
};

} // namespace

TEST_F(ServerTest, StaticFilesAndState) {
    auto c = client();
    const auto page = c.Get("/");
    ASSERT_TRUE(page);
    EXPECT_EQ(page->status, 200);
    EXPECT_EQ(page->body, "<h1>Buddy</h1>");
    EXPECT_EQ(page->get_header_value("X-Frame-Options"), "DENY");

    store.set("buddy/hue/state", R"({"bridge": "ok"})");
    store.set("buddy/hue/status", "online");
    const auto state = c.Get("/api/state");
    ASSERT_TRUE(state);
    EXPECT_EQ(json::parse(state->body), (json{{"buddy/hue/state", {{"bridge", "ok"}}}, {"buddy/hue/status", "online"}}));
}

TEST_F(ServerTest, Commands) {
    auto r = post("/api/cmd/hue", R"({"action": "on", "id": "living"})");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 202);
    ASSERT_EQ(published.size(), 1u);
    EXPECT_EQ(published[0].first, "buddy/hue/cmd");

    EXPECT_EQ(post("/api/cmd/voice", R"({"action": "x"})")->status, 404);
    EXPECT_EQ(post("/api/cmd/hue", R"({"query": "x"})")->status, 400);
    EXPECT_EQ(post("/api/cmd/hue", R"({"action": "on"})", {{"Origin", "http://evil.com"}})->status, 403);
    auto c = client();
    EXPECT_EQ(c.Post("/api/cmd/hue", R"({"action": "on"})", "text/plain")->status, 415);  // cross-site "simple" POST
    EXPECT_EQ(published.size(), 1u);
}

TEST_F(ServerTest, ForeignHostNameIsRefused) {  // DNS rebinding
    auto c = client();
    const auto r = c.Get("/", httplib::Headers{{"Host", "evil.com"}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 403);
}

TEST_F(ServerTest, EventStream) {
    store.set("buddy/ears/state", R"({"state": "idle", "wake_word": "hey buddy", "threshold": 0.5})");
    std::thread later([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        store.set("buddy/ears/state", R"({"state": "listening", "wake_word": "hey buddy", "threshold": 0.5})");
    });
    std::string received;
    auto c = client();
    c.Get("/api/events", [&](const char* data, std::size_t size) {
        received.append(data, size);
        return received.find("listening") == std::string::npos;  // stop after the change
    });
    later.join();
    EXPECT_NE(received.find(R"(data: {"payload":{"state":"idle","threshold":0.5,"wake_word":"hey buddy"},"topic":"buddy/ears/state"})"),
              std::string::npos);
    EXPECT_NE(received.find(R"(data: {"patch":[{"op":"replace","path":"/state","value":"listening"}],"topic":"buddy/ears/state"})"),
              std::string::npos);  // this stream has the previous payload: only the change
    EXPECT_NE(received.find(": keepalive"), std::string::npos);  // 100 ms of quiet > 50 ms keepalive
}
