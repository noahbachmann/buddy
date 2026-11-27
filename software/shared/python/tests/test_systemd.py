import socket

from buddy_shared import systemd


def test_noop_without_socket(monkeypatch):
    monkeypatch.delenv("NOTIFY_SOCKET", raising=False)
    systemd.notify_ready()  # must not raise


def test_ready_and_rate_limited_watchdog(monkeypatch, tmp_path):
    path = str(tmp_path / "notify")
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
        s.bind(path)
        s.settimeout(1)
        monkeypatch.setenv("NOTIFY_SOCKET", path)
        monkeypatch.setenv("WATCHDOG_USEC", "60000000")
        monkeypatch.setattr(systemd, "_last_ping", 0.0)
        systemd.notify_ready()
        assert s.recv(64) == b"READY=1"
        systemd.notify_watchdog()
        systemd.notify_watchdog()  # within WatchdogSec / 4: dropped
        systemd.notify_status("ok")
        assert s.recv(64) == b"WATCHDOG=1"
        assert s.recv(64) == b"STATUS=ok"
