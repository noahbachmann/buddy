#include <gtest/gtest.h>

#include <thread>

#include "state.hpp"

using namespace buddy::web;
using namespace std::chrono_literals;

namespace {

std::vector<std::string> topics(const std::vector<StateStore::Update>& updates) {
    std::vector<std::string> out;
    for (const auto& u : updates) out.push_back(u.topic + "=" + u.payload);
    return out;
}

} // namespace

TEST(StateStore, OnlyWhatChanged) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "1");
    std::uint64_t since = 0;
    EXPECT_EQ(topics(store.wait(since, 0ms)), (std::vector<std::string>{"a=1", "b=1"}));
    store.set("a", "2");
    store.set("a", "3");
    store.set("b", "1");  // unchanged: no update
    EXPECT_EQ(topics(store.wait(since, 0ms)), (std::vector<std::string>{"a=3"}));
    EXPECT_TRUE(store.wait(since, 0ms).empty());
}

TEST(StateStore, WaitWakesOnChangeAndClose) {
    StateStore store;
    std::uint64_t since = 0;
    std::thread writer([&] {
        std::this_thread::sleep_for(20ms);
        store.set("a", "1");
    });
    EXPECT_EQ(topics(store.wait(since, 5s)), (std::vector<std::string>{"a=1"}));
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
