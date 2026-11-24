"""Step 3 (#80): the browse loop. One profile per `step()`: capture → decide → act, inside the active window.

Modes (`BUDDY_SCREENER_BROWSE`): `off` (the service acts on nothing), `dry` (decides, logs, skips; a profile it would
like is held for Dev: the loop stops with it on screen and asks on Telegram; Dev likes it by hand or with
`label/type/send`, then resumes) and `live` (likes too: with the bot's comment, or none when none fitted).
Dry rows are shadow session 5: each keeps its screenshots in tmpfs (`rows/<id>/`) until Dev labels it.

Every act goes through the navigator's checks (the captured profile's name, the section's identity, the comment read
back, the acting allowlist) and into the action log. Anything unexpected → `hold`: the loop stays paused until Dev
resumes (Dev, 2026-11-24: no automatic retry). The like budget (`likes.py`) stops before the app's paywall.
"""

import random
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import likes, shadow
from .decide import BudgetSpent, DecideError, UsageLimited
from .device import AdbError, Stopped
from .library import Library, LibraryError
from .navigator import Navigator, NeedsYou, OutOfLikes
from .chat import Chat
from .store import Store

DRY, LIVE, OFF = "dry", "live", "off"

# loop states (buddy/screener/state)
BROWSING, BREAK, WAITING, DONE_TODAY, LIMIT, NEEDS_YOU, OFFLINE = (
    "browsing", "break", "waiting", "done_today", "limit", "needs_you", "offline")

# what a hold tells Dev (Telegram, panel): fixed texts by kind, never a reason string or a name
HOLD_WOULD_LIKE = "would_like"
HOLD_SCREEN = "screen"
HOLD_LIKES = "likes"
HOLD_DECIDE = "decide"
HOLD_FILES = "files"   # preferences.md / templates.md refused

DECIDE_FAILURES = 3   # decisions failing in a row (timeouts, no answer) before Dev is asked


@dataclass
class BrowseConfig:
    mode: str = OFF
    hours: tuple[float, float] = (10.0, 18.0)   # local hours; with likes_24h the start moves to when likes are back
    gap_s: tuple[float, float] = (20.0, 90.0)   # between two profiles
    break_every: tuple[int, int] = (6, 12)      # profiles between random breaks
    break_min: tuple[float, float] = (5.0, 20.0)
    daily_likes: int = 8
    likes_24h: bool = False


def parse_range(text: str, name: str) -> tuple[float, float]:
    """"a-b" → (a, b), a ≤ b."""
    try:
        a, b = (float(x) for x in text.split("-"))
    except ValueError:
        raise ValueError(f"{name}={text!r}: two numbers a-b") from None
    if not 0 <= a <= b:
        raise ValueError(f"{name}={text!r}: 0 ≤ a ≤ b")
    return a, b


@dataclass
class Window:
    start: float
    end: float

    def open(self, now: float) -> bool:
        return self.start <= now < self.end


@dataclass
class LoopState:
    state: str = WAITING
    until: float | None = None   # break / waiting / limit end
    reason: str = ""             # a kind only, never a name
    last: dict[str, Any] = field(default_factory=dict)   # {action, ts}


class Browser:
    def __init__(self, cfg: BrowseConfig, nav: Navigator, decider, load_lib: Callable[[], Library], store: Store,
                 tmp_dir: Path, hold: Callable[[str, int | None, str], None], notify: Callable[[str], None],
                 mode: Callable[[], str], pages: Callable[[list], list[bytes]] | None = None,
                 clock: Callable[[], float] = time.time, rng: random.Random | None = None,
                 day_start: Callable[[float], float] = likes.local_midnight, log: Callable[[str], None] = print):
        self.cfg, self.nav, self.decider, self.load_lib, self.store = cfg, nav, decider, load_lib, store
        self.tmp_dir, self.hold, self.notify, self.mode, self.pages = tmp_dir, hold, notify, mode, pages
        self.clock, self.rng, self.day_start, self.log = clock, rng or random.Random(), day_start, log
        self.state = LoopState()
        self.break_until = 0.0
        self.next_break = self._break_after()
        self.decide_failures = 0
        self.pending: tuple[int, str, str, Chat] | None = None   # a like the paywall kept in its panel: row, name, text, its chat
        self.told_done: float | None = None                # the batch whose "likes used up" Dev was told about

    # --- schedule ---

    def _break_after(self) -> int:
        lo, hi = self.cfg.break_every
        return self.rng.randint(int(lo), int(hi))

    def window(self, now: float) -> Window:
        """Today's active window. With `likes_24h` (Dev's rule once confirmed): starts when yesterday's used-up likes
        come back, if that's later than the configured start, and keeps its length (likes back 11:30 → 11:30–19:30)."""
        day = self.day_start(now)
        start, end = day + self.cfg.hours[0] * 3600, day + self.cfg.hours[1] * 3600
        if self.cfg.likes_24h:
            st = self._likes(start)
            if not st.left and st.back_at and st.back_at > start:
                end = min(st.back_at + (end - start), day + 86400 - 60)   # not past midnight: the next day's own
                start = st.back_at
        return Window(start, end)

    def _likes(self, now: float) -> likes.LikeStatus:
        return likes.status(self.store.actions(now - 7 * likes.DAY), now, self.cfg.daily_likes, self.cfg.likes_24h,
                            self.day_start)

    def _set(self, state: str, until: float | None = None, reason: str = "") -> None:
        self.state.state, self.state.until, self.state.reason = state, until, reason

    def _act(self, kind: str, row_id: int | None = None, detail: str = "") -> None:
        self.store.add_action(kind, self.cfg.mode, row_id, detail)
        self.state.last = {"action": kind, "ts": round(self.clock())}

    def _hold(self, kind: str, why: str, row_id: int | None = None) -> float:
        """Stop until Dev resumes: logged with `why` (no names: the navigator's reasons carry none), Dev gets `kind`."""
        self._act(likes.NEEDS_YOU if kind != HOLD_WOULD_LIKE else likes.WOULD_LIKE, row_id, why[:120])
        self._set(NEEDS_YOU, reason=kind)
        self.log(f"hold ({kind}): {why}")
        self.hold(kind, row_id, why)
        return 5.0

    # --- one step ---

    def step(self) -> float:
        """One profile, or nothing when outside the window / paused / out of likes. Returns seconds to wait."""
        if self.cfg.mode == OFF:
            self._set(WAITING, reason="off")
            return 300.0
        mode = self.mode()
        if mode != "active":
            self._set(mode, reason=self.state.reason if mode == NEEDS_YOU else "")
            return 5.0
        now = self.clock()
        win = self.window(now)
        if not win.open(now):
            nxt = win.start if now < win.start else self.window(self.day_start(now) + 86400 + 1).start
            self._set(WAITING, nxt)
            return min(nxt - now, 600.0)
        st = self._likes(now)
        if st.stuck:
            return self._hold(HOLD_LIKES, st.stuck)
        if not st.left:
            back = st.back_at or self.window(self.day_start(now) + 86400 + 1).start
            if self.told_done != st.last_like:
                self.told_done = st.last_like
                self.notify(f"likes used up ({likes.describe(st, self.cfg.daily_likes)})")
            self._set(DONE_TODAY, back)
            return min(max(back - now, 1.0), 600.0)
        if now < self.break_until:
            self._set(BREAK, self.break_until)
            return min(self.break_until - now, 600.0)
        if self.pending:
            return self._send_pending(st)
        return self._profile(st)

    def _profile(self, st: likes.LikeStatus) -> float:
        self._set(BROWSING)
        try:
            lib = self.load_lib()
        except LibraryError as e:
            return self._hold(HOLD_FILES, f"library: {e}")
        try:
            self.nav.wake()   # the screen sleeps during gaps and breaks
            r = shadow.judge(self.nav, self.decider, lib, self.store, self.tmp_dir, run=self.cfg.mode)
        except UsageLimited as e:
            self._act(likes.LIMIT, detail="usage limit")
            self.notify(f"usage limit: decisions wait until {time.strftime('%H:%M', time.localtime(e.until))}")
            self._set(LIMIT, e.until, "usage")
            return min(max(e.until - self.clock(), 1.0), 600.0)
        except BudgetSpent:
            self._act(likes.LIMIT, detail="daily $ cap")
            self.notify("daily $ cap reached: decisions wait")
            self._set(LIMIT, self.clock() + 1800, "cost")
            return 1800.0
        except DecideError as e:
            self.decide_failures += 1
            self.log(f"decide failed ({self.decide_failures}): {e}")
            if self.decide_failures >= DECIDE_FAILURES:
                self.decide_failures = 0
                return self._hold(HOLD_DECIDE, f"last: {type(e).__name__}: {str(e)[:80]}")
            return 60.0
        except NeedsYou as e:
            return self._hold(HOLD_SCREEN, e.reason)
        self.decide_failures = 0
        b, name = r.browse, r.capture.profile.name
        self.log(f"#{r.row_id}: {b.decision} ({self.cfg.mode})")
        try:
            if self.cfg.mode == DRY:
                self._keep_pages(r, lib)
                if b.decision == "like":
                    return self._hold(HOLD_WOULD_LIKE, "a like waits for Dev", r.row_id)
                self.nav.skip_profile(name)
                self._act(likes.SKIP, r.row_id)
            elif b.decision == "like":
                text = b.comment_text if b.comment != "none" else ""
                s, sv = self.nav.scroll_to(r.capture, b.comment_on)
                self.nav.open_sheet(s, sv)
                if text:
                    self.nav.type_comment(text)
                liked = shadow.liked_chat(r.capture, b.prompt_langs, b.comment_on, text, self.clock())
                self._send(r.row_id, name, text, st, liked)
            else:
                self.nav.skip_profile(name)
                self._act(likes.SKIP, r.row_id)
        except OutOfLikes as e:
            return self._paywall(e, r.row_id, name, b.comment_text if b.comment != "none" else "",
                                 shadow.liked_chat(r.capture, b.prompt_langs, b.comment_on,
                                                   b.comment_text if b.comment != "none" else "", self.clock()))
        except NeedsYou as e:
            return self._hold(HOLD_SCREEN, e.reason, r.row_id)
        return self._gap()

    def _send(self, row_id: int, name: str, text: str, st: likes.LikeStatus, liked: Chat) -> None:
        self.nav.send_like(name, text)
        self._act(likes.LIKE if text else likes.LIKE_PLAIN, row_id)
        self.store.save_chat(liked)   # kept for its chat once she matches (#77)
        if st.probe and st.last_like is not None:
            hours = (self.clock() - st.last_like) / 3600
            self._act(likes.LIKES_BACK, row_id, f"{hours:.1f} h")
            self.notify(f"likes came back {hours:.1f} h after the last like: the 24 h guess holds")

    def _paywall(self, e: OutOfLikes, row_id: int, name: str, text: str, liked: Chat) -> float:
        self._act(likes.OUT_OF_LIKES, row_id, "moved on" if e.moved_on else "panel kept")
        if not e.moved_on:
            self.pending = (row_id, name, text, liked)   # sent from the open panel once the likes are back
        self.log(f"paywall: {e.reason}")
        return 5.0

    def _send_pending(self, st: likes.LikeStatus) -> float:
        row_id, name, text, liked = self.pending
        self.pending = None
        self._set(BROWSING)
        try:
            self._send(row_id, name, text, st, liked)
        except OutOfLikes as e:
            return self._paywall(e, row_id, name, text, liked)
        except NeedsYou as e:
            return self._hold(HOLD_SCREEN, e.reason, row_id)
        return self._gap()

    def _gap(self) -> float:
        self.next_break -= 1
        if self.next_break <= 0:
            self.next_break = self._break_after()
            minutes = self.rng.uniform(*self.cfg.break_min)
            self.break_until = self.clock() + minutes * 60
            self._act(likes.BREAK, detail=f"{minutes:.0f} min")
        return self.rng.uniform(*self.cfg.gap_s)

    def _keep_pages(self, r: shadow.ShadowResult, lib: Library) -> None:
        """Dry rows (session 5): Dev labels them in batches from these screenshots; tmpfs only, deleted on label."""
        d = rows_dir(self.tmp_dir) / str(r.row_id)
        d.mkdir(parents=True, exist_ok=True, mode=0o700)
        (d / "describe.txt").write_text("\n".join(shadow.describe(r, lib)) + "\n")
        if self.pages is not None:
            for i, page in enumerate(self.pages(self.nav.screenshots()), 1):
                (d / f"page-{i}.jpg").write_bytes(page)
        for f in d.iterdir():
            f.chmod(0o600)

    # --- the worker ---

    def run(self, running: Callable[[], bool], wait: Callable[[float], None],
            on_state: Callable[[], None] = lambda: None, first: Callable[[], float] | None = None) -> None:
        """The worker's loop. `first`: another step on the same phone before each profile (the chats, #77): the
        wait is the shorter of the two."""
        while running():
            try:
                delay = first() if first is not None else float("inf")
                delay = min(delay, self.step())
            except Stopped:
                delay = 5.0
            except AdbError as e:
                self.log(f"phone: {e}")
                self._set(OFFLINE)
                delay = 30.0
            on_state()
            wait(delay)


def rows_dir(tmp_dir: Path) -> Path:
    return tmp_dir / "rows"


def drop_pages(tmp_dir: Path, row_id: int) -> bool:
    d = rows_dir(tmp_dir) / str(row_id)
    if not d.is_dir():
        return False
    for f in d.iterdir():
        f.unlink()
    d.rmdir()
    return True


def today(store: Store, now: float, day_start: Callable[[float], float] = likes.local_midnight) -> dict[str, int]:
    """Counts for the panel (no names)."""
    acts = store.actions(day_start(now))
    n = {k: sum(a.kind == k for a in acts if a.mode in (DRY, LIVE)) for k in
         (likes.SKIP, likes.LIKE, likes.LIKE_PLAIN, likes.WOULD_LIKE)}
    return {"profiles": sum(n.values()), "skips": n[likes.SKIP], "likes": n[likes.LIKE] + n[likes.LIKE_PLAIN],
            "would_like": n[likes.WOULD_LIKE]}
