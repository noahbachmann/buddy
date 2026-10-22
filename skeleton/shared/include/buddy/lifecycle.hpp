#pragma once
// Clean shutdown on Ctrl+C (CLion stop button) and SIGTERM (systemctl stop).

#include <atomic>
#include <csignal>

namespace buddy {

inline std::atomic<bool> g_running{true};

inline bool running() { return g_running.load(); }

inline void install_signal_handlers() {
    auto handler = [](int) { g_running.store(false); };
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
}

} // namespace buddy
