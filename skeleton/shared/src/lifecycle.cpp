#include "buddy/lifecycle.hpp"

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/eventfd.h>
#include <unistd.h>

namespace buddy {

namespace {

std::atomic<bool> g_running{true};
int g_stop_fd = -1;  // handler → signal thread; set before the handlers, never closed

std::mutex g_mutex;
std::condition_variable g_stopped;
bool g_stopping = false;  // under g_mutex: on_stop() runs callbacks at once
bool g_stopped_done = false;  // under g_mutex: callbacks ran, sleep() returns
std::vector<std::function<void()>> g_on_stop;

// Only async-signal-safe calls here: no locks, no allocation.
void handler(int) {
    g_running.store(false);
    const std::uint64_t one = 1;
    [[maybe_unused]] const auto n = write(g_stop_fd, &one, sizeof one);
}

void stop_thread() {
    std::uint64_t value;
    while (read(g_stop_fd, &value, sizeof value) < 0 && errno == EINTR) {}
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(g_mutex);
        g_stopping = true;
        callbacks.swap(g_on_stop);
    }
    for (auto& fn : callbacks) fn();
    {
        std::lock_guard lock(g_mutex);
        g_stopped_done = true;
    }
    g_stopped.notify_all();
}

} // namespace

void install_signal_handlers() {
    g_stop_fd = eventfd(0, EFD_CLOEXEC);
    if (g_stop_fd >= 0) std::thread(stop_thread).detach();  // no eventfd: running() still works, sleep() sleeps it out
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
}

bool running() { return g_running.load(); }

bool sleep(std::chrono::milliseconds timeout) {
    std::unique_lock lock(g_mutex);
    g_stopped.wait_for(lock, timeout, [] { return g_stopped_done; });
    return running();
}

void on_stop(std::function<void()> fn) {
    {
        std::lock_guard lock(g_mutex);
        if (!g_stopping) {
            g_on_stop.push_back(std::move(fn));
            return;
        }
    }
    fn();
}

} // namespace buddy
