#pragma once
// systemd service notifications (buddy@.service: Type=notify, WatchdogSec). Plain datagrams to $NOTIFY_SOCKET,
// no libsystemd needed. Without that variable (CLion, --wav/--test runs) every call is a no-op.
//
//   READY=1   service is up; `systemctl start` returns only now
//   WATCHDOG  "still alive": no ping for WatchdogSec → systemd kills and restarts the service.
//             Ping only from places that prove the service still works (main loop, audio flowing).

#include <string_view>

namespace buddy::systemd {

void notify_ready();

// Cheap to call often: sends at most every WatchdogSec / 4. Thread-safe.
void notify_watchdog();

// Free text for `systemctl status`, e.g. "waiting for the broker".
void notify_status(std::string_view text);

} // namespace buddy::systemd
