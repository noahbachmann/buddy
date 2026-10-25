"""SIGINT/SIGTERM → running() becomes False, like shared/include/buddy/lifecycle.hpp."""

import signal
import threading

_stop = threading.Event()


def install_signal_handlers() -> None:
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: _stop.set())


def running() -> bool:
    return not _stop.is_set()


def sleep(seconds: float) -> bool:
    """Sleeps, but wakes on shutdown. Returns running()."""
    return not _stop.wait(seconds)
