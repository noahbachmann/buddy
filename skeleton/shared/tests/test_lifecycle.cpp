// Own executable and one test: the handlers and the stop state are process-wide, and a stop can't be undone.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>

#include <unistd.h>

#include "buddy/lifecycle.hpp"

using namespace std::chrono_literals;

TEST(Lifecycle, SigtermWakesSleepAndRunsCallbacks) {
    buddy::install_signal_handlers();
    std::atomic<int> calls{0};
    buddy::on_stop([&] { ++calls; });

    EXPECT_TRUE(buddy::sleep(10ms));  // no signal: sleeps it out
    EXPECT_TRUE(buddy::running());

    kill(getpid(), SIGTERM);  // process-directed like systemctl stop: any thread may take it
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(buddy::sleep(5s));
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    EXPECT_FALSE(buddy::running());
    EXPECT_EQ(calls.load(), 1);  // ran before sleep() was woken

    buddy::on_stop([&] { ++calls; });  // already stopping: runs at once
    EXPECT_EQ(calls.load(), 2);
    EXPECT_FALSE(buddy::sleep(5s));  // returns at once
}
