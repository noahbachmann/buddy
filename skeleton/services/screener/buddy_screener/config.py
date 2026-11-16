"""BUDDY_SCREENER_* settings and the kill switch files."""

import os
from dataclasses import dataclass
from pathlib import Path

from buddy_shared.config import env_int, env_string

from .device import Stopped


@dataclass
class Config:
    package: str = ""            # the app's Android package: host only (buddy.env), never in the repo
    serial: str = ""             # adb serial; empty = the only device
    stock_ime: str = ""          # keyboard for Dev while paused; empty = the first other enabled one
    config_dir: Path = Path.home() / "buddy/config/screener"
    data_dir: Path = Path.home() / "buddy/data/screener"
    check_s: int = 30            # phone check interval in the service

    @classmethod
    def from_env(cls) -> "Config":
        d = cls()
        c = cls(
            package=env_string("BUDDY_SCREENER_APP", d.package),
            serial=env_string("BUDDY_SCREENER_SERIAL", d.serial),
            stock_ime=env_string("BUDDY_SCREENER_STOCK_IME", d.stock_ime),
            config_dir=Path(env_string("BUDDY_SCREENER_CONFIG", str(d.config_dir))),
            data_dir=Path(env_string("BUDDY_SCREENER_DATA", str(d.data_dir))),
            check_s=env_int("BUDDY_SCREENER_CHECK_S", d.check_s),
        )
        if not c.package:
            raise ValueError("BUDDY_SCREENER_APP is not set (the app's package, in buddy.env)")
        return c

    @property
    def stop_file(self) -> Path:
        """Kill switch that works without MQTT: `touch` it and the next input action stops."""
        return self.config_dir / "stop"

    @property
    def pause_file(self) -> Path:
        return self.data_dir / "paused"

    @property
    def forbidden_file(self) -> Path:
        return self.config_dir / "forbidden"

    @property
    def tmp_dir(self) -> Path:
        """Raw dumps and screenshots: tmpfs, gone at reboot."""
        run = os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/screener-{os.getuid()}"
        return Path(run) / "screener"

    def mode(self) -> str:
        return "stopped" if self.stop_file.exists() else "paused" if self.pause_file.exists() else "active"

    def guard(self, running=lambda: True) -> None:
        """Before every input action on the phone."""
        if not running():
            raise Stopped("shutting down")
        mode = self.mode()
        if mode != "active":
            raise Stopped(mode)
