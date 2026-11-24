"""BUDDY_SCREENER_* settings and the kill switch files."""

import os
from dataclasses import dataclass
from pathlib import Path

from buddy_shared.config import env_float, env_int, env_string

from .chat import ChatLimits
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
    photo_check: bool = True     # photos-only physical check before the full decision (#83)
    photo_model: str = ""        # its model; empty = `model`
    photo_check_px: int = 1024   # its crops' long side: ≥ the screen's 960 px photos = not downscaled
    check_model: str = ""        # the like comment's copy/invention check (#84); empty = `model`
    browse: str = "off"          # the loop (#80): off | dry (decides, skips, holds likes for Dev) | live
    hours: str = "10-18"         # active window, local hours (likes_24h: starts when the likes are back)
    gap_s: str = "20-90"         # seconds between two profiles
    break_every: str = "6-12"    # profiles between random breaks
    break_min: str = "5-20"      # a break's minutes
    daily_likes: int = 8         # the app's free likes per batch (#80): the budget stops before its paywall
    likes_24h: bool = False      # likes come back 24 h after a used-up batch's last like (Dev's guess, likes.py)
    chat_min_exchanges: int = ChatLimits.min_exchanges   # chat (#77): meanings in chat.ChatLimits
    chat_max_bot: int = ChatLimits.max_bot
    chat_max_days: float = ChatLimits.max_days
    chat_nudge_days: float = ChatLimits.nudge_days
    chat_close_days: float = ChatLimits.close_days
    chat_unmatch_h: float = ChatLimits.unmatch_h
    sim_model: str = "claude-haiku-5-5"   # the sim's personas (they play matches, the chat calls use `model`)
    sim_usd: float = 3.0         # what --sim may spend per 24 h (API-equivalent $), apart from `daily_usd`

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
            photo_check=env_int("BUDDY_SCREENER_PHOTO_CHECK", int(d.photo_check)) != 0,
            photo_model=env_string("BUDDY_SCREENER_PHOTO_MODEL", d.photo_model),
            photo_check_px=env_int("BUDDY_SCREENER_PHOTO_CHECK_PX", d.photo_check_px),
            check_model=env_string("BUDDY_SCREENER_CHECK_MODEL", d.check_model),
            browse=env_string("BUDDY_SCREENER_BROWSE", d.browse),
            hours=env_string("BUDDY_SCREENER_HOURS", d.hours),
            gap_s=env_string("BUDDY_SCREENER_GAP_S", d.gap_s),
            break_every=env_string("BUDDY_SCREENER_BREAK_EVERY", d.break_every),
            break_min=env_string("BUDDY_SCREENER_BREAK_MIN", d.break_min),
            daily_likes=env_int("BUDDY_SCREENER_DAILY_LIKES", d.daily_likes),
            likes_24h=env_int("BUDDY_SCREENER_LIKES_24H", int(d.likes_24h)) != 0,
            chat_min_exchanges=env_int("BUDDY_SCREENER_CHAT_MIN_EXCHANGES", d.chat_min_exchanges),
            chat_max_bot=env_int("BUDDY_SCREENER_CHAT_MAX_BOT", d.chat_max_bot),
            chat_max_days=env_float("BUDDY_SCREENER_CHAT_MAX_DAYS", d.chat_max_days),
            chat_nudge_days=env_float("BUDDY_SCREENER_CHAT_NUDGE_DAYS", d.chat_nudge_days),
            chat_close_days=env_float("BUDDY_SCREENER_CHAT_CLOSE_DAYS", d.chat_close_days),
            chat_unmatch_h=env_float("BUDDY_SCREENER_CHAT_UNMATCH_H", d.chat_unmatch_h),
            sim_model=env_string("BUDDY_SCREENER_SIM_MODEL", d.sim_model),
            sim_usd=env_float("BUDDY_SCREENER_SIM_USD", d.sim_usd),
        )
        for key, px in (("PHOTO_PX", c.photo_px), ("PHOTO_CHECK_PX", c.photo_check_px)):
            if not 256 <= px <= 1568:
                raise ValueError(f"BUDDY_SCREENER_{key}={px}: 256..1568")
        if c.effort not in ("low", "medium", "high", "xhigh", "max"):
            raise ValueError(f"BUDDY_SCREENER_EFFORT={c.effort!r}: low|medium|high|xhigh|max")
        if not 1 <= c.daily_likes <= 100:
            raise ValueError(f"BUDDY_SCREENER_DAILY_LIKES={c.daily_likes}: 1..100")
        if c.browse not in ("off", "dry", "live"):
            raise ValueError(f"BUDDY_SCREENER_BROWSE={c.browse!r}: off|dry|live")
        c.browse_config()   # the ranges parse
        if not c.package:
            raise ValueError("BUDDY_SCREENER_APP is not set (the app's package, in buddy.env)")
        return c

    def browse_config(self):
        from .browse import BrowseConfig, parse_range
        hours = parse_range(self.hours, "BUDDY_SCREENER_HOURS")
        if hours[1] > 24:
            raise ValueError(f"BUDDY_SCREENER_HOURS={self.hours!r}: hours of a day")
        every = parse_range(self.break_every, "BUDDY_SCREENER_BREAK_EVERY")
        return BrowseConfig(self.browse, hours, parse_range(self.gap_s, "BUDDY_SCREENER_GAP_S"),
                            (int(every[0]), int(every[1])), parse_range(self.break_min, "BUDDY_SCREENER_BREAK_MIN"),
                            self.daily_likes, self.likes_24h)

    def chat_limits(self) -> ChatLimits:
        return ChatLimits(self.chat_min_exchanges, self.chat_max_bot, self.chat_max_days, self.chat_nudge_days,
                          self.chat_close_days, self.chat_unmatch_h)

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

    @property
    def needs_you_file(self) -> Path:
        """The loop holds for Dev (#80): JSON {kind, row, ask}; `resume` removes it."""
        return self.data_dir / "needs_you"

    def mode(self) -> str:
        if self.stop_file.exists():
            return "stopped"
        if self.needs_you_file.exists():
            return "needs_you"
        return "paused" if self.pause_file.exists() else "active"

    def guard(self, running=lambda: True, hold_ok: bool = False) -> None:
        """Before every input action on the phone. `hold_ok`: Dev's own commands work while the loop holds for Dev
        (that's how Dev handles a held profile); the loop's don't."""
        if not running():
            raise Stopped("shutting down")
        mode = self.mode()
        if mode != "active" and not (hold_ok and mode == "needs_you"):
            raise Stopped(mode)
