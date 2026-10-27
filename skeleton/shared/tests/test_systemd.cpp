// Own executable and one test: the watchdog state (interval read once, last ping) is process-wide.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "buddy/systemd.hpp"

namespace {

// Abstract AF_UNIX datagram socket standing in for systemd's: NOTIFY_SOCKET="@name".
struct NotifySocket {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    std::string name = "buddy-test-notify-" + std::to_string(getpid());

    NotifySocket() {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path + 1, name.data(), name.size());  // sun_path[0] = '\0': abstract
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), static_cast<socklen_t>(sizeof(sa_family_t) + 1 + name.size())))
            throw std::runtime_error("bind failed");
        timeval timeout{.tv_sec = 1, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    }
    ~NotifySocket() { close(fd); }

    std::string recv() const {
        char buf[64];
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        return n < 0 ? "<timeout>" : std::string(buf, static_cast<std::size_t>(n));
    }
};

} // namespace

TEST(Systemd, ReadyRateLimitedWatchdogStatus) {
    unsetenv("NOTIFY_SOCKET");
    buddy::systemd::notify_ready();  // no-op outside systemd

    NotifySocket s;
    setenv("NOTIFY_SOCKET", ("@" + s.name).c_str(), 1);
    setenv("WATCHDOG_USEC", "60000000", 1);
    buddy::systemd::notify_ready();
    EXPECT_EQ(s.recv(), "READY=1");
    buddy::systemd::notify_watchdog();
    buddy::systemd::notify_watchdog();  // within WatchdogSec / 4: dropped
    buddy::systemd::notify_status("ok");
    EXPECT_EQ(s.recv(), "WATCHDOG=1");
    EXPECT_EQ(s.recv(), "STATUS=ok");
    unsetenv("NOTIFY_SOCKET");
}
