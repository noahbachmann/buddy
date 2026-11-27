"""The like budget (#80): how many of the app's free likes are left, and when used-up ones come back.

Past the free likes "send like" opens a paywall, and closing it can drop the profile with its typed comment (#32, #47,
#129). So the budget is counted from our own action log (`store.actions`), never read from the app, and the loop
stops before the paywall. Likes Dev sends by hand aren't in the log: the paywall still catches those.

When likes come back (measured 2026-11-25, every like in the log fits it): **at most `daily` likes in any 24 h
window**. A like frees its slot 24 h after it was sent; there is no reset at midnight or after a batch (Dev's
earlier guess, 24 h after a used-up batch's last like, was off: the app took a like at 10:24 that the guess put at
14:39, and it showed the paywall at 14:19 with 8 likes in the 24 h before). So `left` = `daily` minus the likes of the
last 24 h, and with none left they come back when the oldest of them is 24 h old.
A paywall although the log has slots left (Dev liked by hand, or the rule is off) → wait until the oldest like
before it is 24 h old, then try once (`probe`); a paywall without any like in the 24 h before it, or again right
after that wait → `stuck`: Dev.
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
    used: int                      # likes in the last 24 h
    left: int
    back_at: float | None = None   # left == 0: when the next slot frees (the oldest of them turns 24 h)
    probe: bool = False            # a paywall wait is over: the next like tests the rule
    stuck: str = ""                # a paywall the rule can't explain: why (Dev looks)
    last_like: float | None = None


def status(actions: Sequence[Action], now: float, daily: int) -> LikeStatus:
    likes = sorted(a.ts for a in actions if a.kind in LIKES and a.ts <= now)
    walls = sorted(a.ts for a in actions if a.kind == OUT_OF_LIKES and a.ts <= now)
    last = likes[-1] if likes else None
    window = [ts for ts in likes if ts > now - DAY]
    if walls and (last is None or walls[-1] > last):
        # the latest paywall had no like after it: its slot frees when the oldest like before it turns 24 h
        wall = walls[-1]
        before = [ts for ts in likes if wall - DAY < ts <= wall]
        if not before:
            return LikeStatus(len(window), 0, None, stuck="paywall without a like in the 24 h before it",
                              last_like=last)
        back = before[0] + DAY
        if now < back:
            return LikeStatus(len(window), 0, back, last_like=last)
        return LikeStatus(len(window), max(1, daily - len(window)), probe=True, last_like=last)
    left = max(0, daily - len(window))
    return LikeStatus(len(window), left, window[-daily] + DAY if left == 0 else None, last_like=last)


def describe(st: LikeStatus, daily: int) -> str:
    if st.stuck:
        return f"likes: {st.stuck}"
    if st.left:
        return f"likes: {st.used}/{daily} used" + (" (first after a paywall wait)" if st.probe else "")
    when = time.strftime("%Y-%m-%d %H:%M", time.localtime(st.back_at)) if st.back_at else "later"
    return f"likes: {st.used}/{daily} used in 24 h, the next one at {when}"
