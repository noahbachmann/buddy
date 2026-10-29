#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "buddy/queue.hpp"

using namespace std::chrono_literals;

TEST(BlockingQueue, FifoAndTimeout) {
    buddy::BlockingQueue<int> q;
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.pop(0ms), 1);
    EXPECT_EQ(q.pop(0ms), 2);
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(q.pop(50ms), std::nullopt);
    EXPECT_GE(std::chrono::steady_clock::now() - t0, 50ms);
}

TEST(BlockingQueue, PushWakesAWaitingPop) {
    buddy::BlockingQueue<int> q;
    std::thread producer([&] {
        std::this_thread::sleep_for(20ms);
        q.push(7);
    });
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(q.pop(5s), 7);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    producer.join();
}

TEST(BlockingQueue, CloseWakesPopForGood) {
    buddy::BlockingQueue<int> q;
    std::thread closer([&] {
        std::this_thread::sleep_for(20ms);
        q.close();
    });
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(q.pop(5s), std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    closer.join();
    q.push(1);
    EXPECT_EQ(q.pop(5s), std::nullopt);  // closed: no more waiting, items or not
}
