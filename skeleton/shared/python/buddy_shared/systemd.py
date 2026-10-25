"""systemd notifications (buddy@.service: Type=notify, WatchdogSec), like shared/src/systemd.cpp.

Plain datagrams to $NOTIFY_SOCKET, no libsystemd. Without that variable (local runs, tests) every call is a no-op.
    READY=1   service is up; `systemctl start` returns only now
    WATCHDOG  "still alive": no ping for WatchdogSec → systemd kills and restarts the service.
              Ping only from places that prove the service still works (main loop).
"""

import os
import socket
import threading
import time

_lock = threading.Lock()
_last_ping = 0.0


def _send(msg: str) -> None:
    path = os.environ.get("NOTIFY_SOCKET", "")
    if not path:
        return
    if path.startswith("@"):
        path = "\0" + path[1:]  # abstract socket
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM | socket.SOCK_CLOEXEC) as s:
            s.sendto(msg.encode(), path)
    except OSError:
        pass


def notify_ready() -> None:
    _send("READY=1")


def notify_watchdog() -> None:
    """Cheap to call often: sends at most every WatchdogSec / 4. Thread-safe."""
    global _last_ping
    interval = int(os.environ.get("WATCHDOG_USEC", "0") or 0) / 4e6
    if interval <= 0:
        return
    now = time.monotonic()
    with _lock:
        if now - _last_ping < interval:
            return
        _last_ping = now
    _send("WATCHDOG=1")


def notify_status(text: str) -> None:
    """Free text for `systemctl status`, e.g. "waiting for the broker"."""
    _send("STATUS=" + text)
