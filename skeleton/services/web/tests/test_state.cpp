#include <gtest/gtest.h>

#include <thread>

#include "state.hpp"

using namespace buddy::web;
using namespace std::chrono_literals;
using json = nlohmann::json;

namespace {

// Frames as their JSON events, checking the SSE framing on the way
std::vector<json> events(const std::vector<Frame>& frames) {
    std::vector<json> out;
    for (const auto& f : frames) {
        EXPECT_TRUE(f->starts_with("data: ") && f->ends_with("\n\n")) << *f;
        out.push_back(json::parse(f->substr(6)));
    }
    return out;
}

json full(const std::string& topic, json payload) { return {{"topic", topic}, {"payload", std::move(payload)}}; }

// Big enough that a one-field change is cheaper as a patch
json lamp(int brightness) {
    return {{"name", "Bedroom Ceiling"}, {"id", "36c12aaf-18c8-429a-ae02-0bf6b6b21eb6"}, {"on", true},
            {"brightness", brightness}, {"xy", {0.6744, 0.3211}}};
}

} // namespace

TEST(StateStore, OnlyWhatChanged) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "1");
    std::uint64_t since = 0;
    EXPECT_EQ(events(store.wait(since, 0ms)), (std::vector<json>{full("a", 1), full("b", 1)}));
    store.set("a", "2");
    store.set("a", "3");
    store.set("b", "1");  // unchanged: no update
    EXPECT_EQ(events(store.wait(since, 0ms)), (std::vector<json>{full("a", 3)}));
    EXPECT_TRUE(store.wait(since, 0ms).empty());
}

TEST(StateStore, PayloadsAsJsonOrString) {
    StateStore store;
    store.set("s", "online");
    store.set("j", R"({"on": true})");
    store.set("gone", "x");
    store.set("gone", "");  // retained message cleared
    std::uint64_t since = 0;
    EXPECT_EQ(events(store.wait(since, 0ms)),
              (std::vector<json>{full("gone", nullptr), full("j", {{"on", true}}), full("s", "online")}));
    EXPECT_EQ(store.snapshot(), (json{{"gone", nullptr}, {"j", {{"on", true}}}, {"s", "online"}}));
}

TEST(StateStore, PatchForClientsThatHaveThePreviousPayload) {
    StateStore store;
    store.set("hue", lamp(26).dump());
    std::uint64_t has_v1 = 0;
    store.wait(has_v1, 0ms);
    store.set("hue", lamp(40).dump());

    const auto e = events(store.wait(has_v1, 0ms));
    ASSERT_EQ(e.size(), 1u);
    EXPECT_EQ(e[0]["topic"], "hue");
    ASSERT_TRUE(e[0].contains("patch")) << e[0];
    EXPECT_EQ(e[0]["patch"], json::parse(R"([{"op": "replace", "path": "/brightness", "value": 40}])"));
    EXPECT_EQ(lamp(26).patch(e[0]["patch"]), lamp(40));

    std::uint64_t fresh = 0;  // new browser: has nothing to patch
    EXPECT_EQ(events(store.wait(fresh, 0ms)), (std::vector<json>{full("hue", lamp(40))}));
}

TEST(StateStore, FullPayloadAfterAMissedChange) {
    StateStore store;
    store.set("hue", lamp(26).dump());
    std::uint64_t since = 0;
    store.wait(since, 0ms);
    store.set("hue", lamp(40).dump());
    store.set("hue", lamp(60).dump());  // the patch is 40 → 60, this client has 26
    EXPECT_EQ(events(store.wait(since, 0ms)), (std::vector<json>{full("hue", lamp(60))}));
}

TEST(StateStore, PatchOnlyWhenSmaller) {
    StateStore store;
    store.set("a", R"({"x": 1})");
    std::uint64_t since = 0;
    store.wait(since, 0ms);
    store.set("a", R"({"x": 2})");  // [{"op":"replace","path":"/x","value":2}] is longer than {"x":2}
    EXPECT_EQ(events(store.wait(since, 0ms)), (std::vector<json>{full("a", {{"x", 2}})}));

    store.set("a", "offline");  // no longer JSON: nothing to patch
    EXPECT_EQ(events(store.wait(since, 0ms)), (std::vector<json>{full("a", "offline")}));
}

TEST(StateStore, FramesAreShared) {
    StateStore store;
    store.set("a", "1");
    std::uint64_t one = 0, two = 0;
    EXPECT_EQ(store.wait(one, 0ms)[0].get(), store.wait(two, 0ms)[0].get());  // encoded once for every browser
}

TEST(StateStore, WaitWakesOnChangeAndClose) {
    StateStore store;
    std::uint64_t since = 0;
    std::thread writer([&] {
        std::this_thread::sleep_for(20ms);
        store.set("a", "1");
    });
    EXPECT_EQ(events(store.wait(since, 5s)), (std::vector<json>{full("a", 1)}));
    writer.join();

    std::thread closer([&] {
        std::this_thread::sleep_for(20ms);
        store.close();
    });
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(store.wait(since, 5s).empty());
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 2s);
    closer.join();
}
