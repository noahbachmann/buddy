"""The like budget (#80): counted from the action log, paywall waits, Dev's 24 h guess."""

from buddy_screener import likes
from buddy_screener.likes import DAY, LIKE, LIKE_PLAIN, OUT_OF_LIKES, SKIP
from buddy_screener.store import Action, Store

H = 3600.0
T0 = 100 * DAY   # midnight (UTC days in these tests)


def utc_day(ts):
    return ts - ts % DAY


def acts(*pairs, mode="live"):
    return [Action(ts, kind, mode) for ts, kind in pairs]


def st(actions, now, rolling=False, daily=8):
    return likes.status(actions, now, daily, rolling, day_start=utc_day)


def test_counts_the_days_likes_and_stops_at_the_budget():
    a = acts(*[(T0 + 10 * H + i * 60, LIKE if i % 2 else LIKE_PLAIN) for i in range(7)], (T0 + 11 * H, SKIP))
    s = st(a, T0 + 12 * H)
    assert (s.used, s.left) == (7, 1)
    a += acts((T0 + 12 * H, LIKE))
    s = st(a, T0 + 12 * H + 1)
    assert (s.used, s.left, s.back_at) == (8, 0, None)            # day mode: tomorrow
    assert st(a, T0 + DAY + 10 * H).left == 8                      # the next day
    assert "none left until tomorrow" in likes.describe(s, 8)


def test_paywall_waits_24h_after_the_last_like_then_probes():
    last = T0 + 11.5 * H
    a = acts(*[(last - i * 60, LIKE) for i in range(8)])
    a += acts((T0 + DAY + 10 * H, OUT_OF_LIKES))                   # the next morning: not back yet
    s = st(a, T0 + DAY + 10 * H + 60)
    assert (s.left, s.back_at) == (0, last + DAY)
    s = st(a, last + DAY)
    assert s.left == 8 and s.probe and s.last_like == last
    a += acts((last + DAY + 60, LIKE))                             # it went through: the guess holds
    s = st(a, last + DAY + 120)
    assert (s.used, s.left, s.probe) == (1, 7, False)


def test_a_paywall_the_guess_cant_explain_is_stuck():
    assert st(acts((T0 + 10 * H, OUT_OF_LIKES)), T0 + 11 * H).stuck          # Dev liked by hand
    last = T0 + 11 * H
    a = acts((last, LIKE), (T0 + DAY + 10 * H, OUT_OF_LIKES), (last + DAY + 60, OUT_OF_LIKES))
    s = st(a, last + DAY + 120)
    assert s.stuck and s.left == 0 and "again" in s.stuck


def test_rolling_batches_come_back_24h_after_their_last_like():
    a = acts(*[(T0 + 10 * H + i * 0.2 * H, LIKE) for i in range(8)])          # used up at 11:24
    end = T0 + 10 * H + 7 * 0.2 * H
    s = st(a, T0 + 12 * H, rolling=True)
    assert (s.left, s.back_at) == (0, end + DAY)
    assert st(a, T0 + DAY + 10 * H, rolling=True).left == 0                    # not yet at 10:00
    assert st(a, end + DAY, rolling=True).left == 8
    # a batch the day ended early on keeps counting until 24 h after its last like
    b = acts(*[(T0 + 17 * H + i * 60, LIKE) for i in range(5)], (T0 + DAY + 10 * H, LIKE))
    assert st(b, T0 + DAY + 11 * H, rolling=True).used == 6


def test_store_keeps_actions_30_days(tmp_path):
    db = tmp_path / "s.db"
    s = Store(db, clock=lambda: 40 * DAY)
    s.add_action(LIKE, "live", 7, ts=5 * DAY)
    s.add_action(SKIP, "dry", 8, "x")
    assert [(a.kind, a.row_id, a.detail) for a in s.actions()] == [(LIKE, 7, ""), (SKIP, 8, "x")]
    assert [a.kind for a in Store(db, clock=lambda: 40 * DAY).actions()] == [SKIP]   # 35 days old: gone
