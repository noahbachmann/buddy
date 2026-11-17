"""BUDDY_SCREENER_* settings and the kill switch files."""

import os
from dataclasses import dataclass
from pathlib import Path

from buddy_shared.config import env_float, env_int, env_string

from .device import Stopped


@dataclass
class Config:
    package: str = ""            # the app's Android package: host only (buddy.env), never in the repo
    serial: str = ""             # adb serial; empty = the only device
    stock_ime: str = ""          # keyboard for Dev while paused; empty = the first other enabled one
    config_dir: Path = Path.home() / "buddy/config/screener"
    data_dir: Path = Path.home() / "buddy/data/screener"
    check_s: int = 30            # phone check interval in the service
    model: str = "claude-sonnet-5-5"   # profile decisions (#66: Sonnet; larger only if a gate fails)
    effort: str = "medium"
    decide_timeout_s: float = 180.0
    daily_usd: float = 5.0       # cap on the SDK's API-equivalent $ per 24 h (the plan isn't billed per call)
    photo_px: int = 512          # long side of a photo crop for the decision (~440 image tokens at 512)
    text_first: bool = True      # text-only dealbreaker check before sending photos

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
            model=env_string("BUDDY_SCREENER_MODEL", d.model),
            effort=env_string("BUDDY_SCREENER_EFFORT", d.effort),
            decide_timeout_s=env_float("BUDDY_SCREENER_DECIDE_TIMEOUT_S", d.decide_timeout_s),
            daily_usd=env_float("BUDDY_SCREENER_DAILY_USD", d.daily_usd),
            photo_px=env_int("BUDDY_SCREENER_PHOTO_PX", d.photo_px),
            text_first=env_int("BUDDY_SCREENER_TEXT_FIRST", int(d.text_first)) != 0,
        )
        if not 256 <= c.photo_px <= 1568:
            raise ValueError(f"BUDDY_SCREENER_PHOTO_PX={c.photo_px}: 256..1568")
        if c.effort not in ("low", "medium", "high", "xhigh", "max"):
            raise ValueError(f"BUDDY_SCREENER_EFFORT={c.effort!r}: low|medium|high|xhigh|max")
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
    def token_file(self) -> Path:
        """`claude setup-token`'s long-lived token for Dev's plan (0600)."""
        return self.config_dir / "claude-token"

    @property
    def limit_file(self) -> Path:
        """Unix time until which the plan's usage limit is hit: no decisions before."""
        return self.data_dir / "limit_until"

    @property
    def db_path(self) -> Path:
        return self.data_dir / "screener.db"

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
