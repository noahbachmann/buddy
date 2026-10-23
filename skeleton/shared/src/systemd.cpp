#include "buddy/systemd.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace buddy::systemd {

namespace {

using Clock = std::chrono::steady_clock;

void send(std::string_view msg) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::size_t len = std::strlen(path);
    if (len >= sizeof addr.sun_path) return;
    std::memcpy(addr.sun_path, path, len);
    if (addr.sun_path[0] == '@') addr.sun_path[0] = '\0';  // abstract socket

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sendto(fd, msg.data(), msg.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&addr),
           static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + len));
    close(fd);
}

// WatchdogSec / 4 (systemd recommends pinging at least every half), 0 = no watchdog configured
Clock::duration ping_interval() {
    static const Clock::duration interval = [] {
        const char* usec = std::getenv("WATCHDOG_USEC");
        const long long us = usec ? std::atoll(usec) : 0;
        return std::chrono::duration_cast<Clock::duration>(std::chrono::microseconds(us / 4));
    }();
    return interval;
}

} // namespace

void notify_ready() { send("READY=1"); }

void notify_watchdog() {
    const auto interval = ping_interval();
    if (interval <= Clock::duration::zero()) return;
    static std::atomic<Clock::rep> last{0};
    const auto now = Clock::now().time_since_epoch().count();
    auto prev = last.load(std::memory_order_relaxed);
    if (now - prev < interval.count()) return;
    if (!last.compare_exchange_strong(prev, now)) return;  // another thread just pinged
    send("WATCHDOG=1");
}

void notify_status(std::string_view text) { send("STATUS=" + std::string(text)); }

} // namespace buddy::systemd
