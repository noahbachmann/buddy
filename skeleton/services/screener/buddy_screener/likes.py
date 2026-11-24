"""The like budget (#80): how many of the app's free likes are left, and when used-up ones come back.

Past the free likes "send like" opens a paywall, and closing it can drop the profile with its typed comment (#32, #47,
#129). So the budget is counted from our own action log (`store.actions`), never read from the app, and the loop
stops before the paywall. Likes Dev sends by hand aren't in the log: the paywall still catches those.

When likes come back isn't known. Dev's guess (2026-11-24): 24 h after the last like of a used-up batch.
- `rolling=False` (until the guess is confirmed): a batch = the likes since local midnight. A paywall anyway → wait
  until the last like before it + 24 h, then try once more. The first like that goes through after that wait is
  logged as `likes_back` (the guess holds): then `BUDDY_SCREENER_LIKES_24H=1`.
- `rolling=True`: a batch = likes less than 24 h apart; used up → back 24 h after its last like (the loop's active
  window starts then the next day).
A paywall the guess can't explain (no like in the 24 h before it, or a second one right after a wait) → `stuck`: Dev.
"""

import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass

from .store import Action

DAY = 86400.0

# action kinds
SKIP = "skip"
LIKE = "like"                 # with a comment
LIKE_PLAIN = "like_plain"     # no comment fitted (Dev, 2026-11-24: like anyway, on the bot's section)
WOULD_LIKE = "would_like"     # dry mode: held for Dev
OUT_OF_LIKES = "out_of_likes"  # the paywall came up: nothing sent
LIKES_BACK = "likes_back"     # the first like after a paywall wait went through: detail = hours after the last like
NEEDS_YOU = "needs_you"
LIMIT = "limit"               # usage limit / daily $ cap: detail = which
BREAK = "break"
LIKES = (LIKE, LIKE_PLAIN)


def local_midnight(ts: float) -> float:
    t = time.localtime(ts)
    return time.mktime((t.tm_year, t.tm_mon, t.tm_mday, 0, 0, 0, 0, 0, -1))


@dataclass
class LikeStatus:
    used: int
    left: int
    back_at: float | None = None   # left == 0: when they come back; None = the next day
    probe: bool = False            # a paywall wait is over: the next like tests the 24 h guess
    stuck: str = ""                # a paywall the guess can't explain: why (Dev looks)
    last_like: float | None = None


def status(actions: Sequence[Action], now: float, daily: int, rolling: bool,
           day_start: Callable[[float], float] = local_midnight) -> LikeStatus:
    likes = sorted(a.ts for a in actions if a.kind in LIKES and a.ts <= now)
    walls = sorted(a.ts for a in actions if a.kind == OUT_OF_LIKES and a.ts <= now)
    last = likes[-1] if likes else None
    if walls and (last is None or walls[-1] > last):
        # the latest paywall had no like after it: wait for the guess, or Dev
        wall = walls[-1]
        if last is not None and wall >= last + DAY and any(last < w < last + DAY for w in walls[:-1]):
            return LikeStatus(0, 0, None, stuck="paywall again after waiting 24 h since the last like", last_like=last)
        if last is None or wall - last >= DAY:
            return LikeStatus(0, 0, None, stuck="paywall without a like in the 24 h before it", last_like=last)
        back = last + DAY
        if now < back:
            return LikeStatus(daily, 0, back, last_like=last)
        return LikeStatus(0, daily, probe=True, last_like=last)
    if rolling:
        batch = []
        for ts in reversed(likes):
            if batch and batch[-1] - ts >= DAY or now - ts >= DAY and not batch:
                break
            batch.append(ts)
        used = len(batch)
        left = max(0, daily - used)
        return LikeStatus(used, left, last + DAY if left == 0 and last is not None else None, last_like=last)
    used = sum(ts >= day_start(now) for ts in likes)
    return LikeStatus(used, max(0, daily - used), None, last_like=last)


def describe(st: LikeStatus, daily: int) -> str:
    if st.stuck:
        return f"likes: {st.stuck}"
    if st.left:
        return f"likes: {st.used}/{daily} used" + (" (first after a paywall wait)" if st.probe else "")
    when = time.strftime("%Y-%m-%d %H:%M", time.localtime(st.back_at)) if st.back_at else "tomorrow"
    return f"likes: {st.used}/{daily} used, none left until {when}"
