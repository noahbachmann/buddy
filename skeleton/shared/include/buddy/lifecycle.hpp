#pragma once
// Clean shutdown on Ctrl+C (CLion stop button) and SIGTERM (systemctl stop). Main loops sleep with sleep() or wait on
// a queue that on_stop() closes, so an idle service doesn't wake just to check running().

#include <chrono>
#include <functional>

namespace buddy {

// Call first in main(): starts the thread that turns the signal into on_stop() callbacks and wakes sleep().
void install_signal_handlers();

bool running();

// Sleeps up to `timeout`, returns early on stop. Returns running() (like buddy_shared.lifecycle.sleep).
bool sleep(std::chrono::milliseconds timeout);

// Runs `fn` once on stop, on the signal thread (at once if already stopping). E.g. close a BlockingQueue.
void on_stop(std::function<void()> fn);

} // namespace buddy
