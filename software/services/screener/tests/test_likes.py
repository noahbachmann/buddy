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


def st(actions, now, daily=8):
    return likes.status(actions, now, daily)


def test_at_most_eight_likes_in_any_24_hours():
    a = acts(*[(T0 + 10 * H + i * 60, LIKE if i % 2 else LIKE_PLAIN) for i in range(7)], (T0 + 11 * H, SKIP))
    s = st(a, T0 + 12 * H)
    assert (s.used, s.left) == (7, 1)
    a += acts((T0 + 12 * H, LIKE))
    s = st(a, T0 + 12 * H + 1)
    assert (s.used, s.left, s.back_at) == (8, 0, T0 + 10 * H + DAY)   # the oldest turns 24 h: one slot back
    assert st(a, T0 + DAY + 9 * H).left == 0                         # no reset at midnight
    assert st(a, T0 + DAY + 10 * H + 30).left == 1
    assert st(a, T0 + DAY + 12 * H + 1).left == 8
    assert "the next one at" in likes.describe(s, 8)


def test_live_evidence_2026_11_25():
    # 8 likes from 13:48 one day to 14:39 the next; the day after, the app took likes from 10:24 and showed the
    # paywall at 14:19 after 7 more (the batch guess said: nothing before 14:39)
    d0 = T0
    a = acts(*[(d0 + h * H, LIKE) for h in (13.8, 15, 16, 17, 21.6, 24 + 9.8, 24 + 10.3, 24 + 11.1, 24 + 12, 24 + 14.65)])
    a = acts(*[(d0 + 24 * H + h * H, LIKE) for h in (9.8, 10.3, 11.1, 12, 14.65)])     # the day before: 5 in its last 24 h
    day = d0 + 2 * DAY
    assert st(a, day + 10.4 * H).left == 5                                              # 10:24: 3 still in the window
    a += acts(*[(day + h * H, LIKE) for h in (10.4, 11.8, 12.27, 12.48, 12.7, 12.92, 13.23)])
    assert st(a, day + 14.3 * H).left == 0                                              # 14:19: the paywall, rightly
    assert st(a, day + 14.3 * H).back_at == d0 + 24 * H + 14.65 * H + DAY               # 14:39


def test_paywall_waits_for_the_oldest_like_before_it_then_probes():
    a = acts(*[(T0 + 10 * H + i * H, LIKE) for i in range(5)])                         # 5 logged, 3 by hand
    wall = T0 + 15 * H
    a += acts((wall, OUT_OF_LIKES))
    s = st(a, wall + 60)
    assert (s.left, s.back_at) == (0, T0 + 10 * H + DAY)
    s = st(a, T0 + 10 * H + DAY)
    assert s.probe and s.left >= 1
    a += acts((T0 + 10 * H + DAY + 60, LIKE))                                         # it went through
    s = st(a, T0 + 10 * H + DAY + 120)
    assert not s.probe and s.used == 5


def test_a_paywall_the_rule_cant_explain_is_stuck():
    assert st(acts((T0 + 10 * H, OUT_OF_LIKES)), T0 + 11 * H).stuck          # no like in the 24 h before it
    a = acts((T0 + 11 * H, LIKE), (T0 + 20 * H, OUT_OF_LIKES), (T0 + 11 * H + DAY + 60, OUT_OF_LIKES))
    s = st(a, T0 + 11 * H + DAY + 120)
    assert s.stuck and s.left == 0                                             # again after the wait


def test_store_keeps_actions_30_days(tmp_path):
    db = tmp_path / "s.db"
    s = Store(db, clock=lambda: 40 * DAY)
    s.add_action(LIKE, "live", 7, ts=5 * DAY)
    s.add_action(SKIP, "dry", 8, "x")
    assert [(a.kind, a.row_id, a.detail) for a in s.actions()] == [(LIKE, 7, ""), (SKIP, 8, "x")]
    assert [a.kind for a in Store(db, clock=lambda: 40 * DAY).actions()] == [SKIP]   # 35 days old: gone
