"""The browse loop (#80) on the fake phone: dry and live, the like budget, the window, holds."""

import random

import pytest

from buddy_screener import browse, likes
from buddy_screener.browse import Browser, BrowseConfig
from buddy_screener.decide import Browse, DecideError, UsageLimited
from buddy_screener.store import Store
from fake_phone import FakePhone
from test_likes import DAY, H, T0, utc_day
from test_shadow import LIB, FakeDecider, like, nav

SKIP = Browse("skip", "none", "none", "", [], [], [], "generic")


class Clock:
    def __init__(self, t):
        self.t = t

    def __call__(self):
        return self.t


class Rig:
    def __init__(self, tmp_path, bot: Browse, mode=browse.DRY, now=T0 + 12 * H, **cfg):
        self.phone = FakePhone("feed-01")
        self.phone.upsell = False
        self.clock = Clock(now)
        self.store = Store(":memory:", clock=self.clock)
        self.decider = FakeDecider(bot)
        self.holds, self.whys, self.notes, self.paused = [], [], [], False
        self.tmp = tmp_path
        self.b = Browser(BrowseConfig(mode=mode, **cfg), nav(self.phone), self.decider, lambda: LIB, self.store,
                         tmp_path, hold=self.hold, notify=self.notes.append,
                         mode=lambda: "needs_you" if self.paused else "active", pages=self.pages,
                         clock=self.clock, rng=random.Random(1), day_start=utc_day, log=lambda s: None)
        self.phone.screencap = lambda half=False: self.phone.screen.encode()

    def pages(self, shots):
        self.shots = list(shots)
        return [b"jpg"] * 2

    def hold(self, kind, row_id, why):
        self.holds.append((kind, row_id))
        self.whys.append(why)
        self.paused = True

    def kinds(self):
        return [a.kind for a in self.store.actions()]


def test_off_does_nothing(tmp_path):
    r = Rig(tmp_path, SKIP, mode=browse.OFF)
    assert r.b.step() == 300.0 and r.phone.actions == []


def test_dry_skips_and_keeps_the_rows_pages(tmp_path):
    r = Rig(tmp_path, SKIP)
    gap = r.b.step()
    assert 20 <= gap <= 90 and r.kinds() == [likes.SKIP]
    assert r.phone.actions[0] == ("key", "feed-01", 224)                  # woken first
    assert [a[2] for a in r.phone.sent()] == ["Skip Alex"]
    row = r.store.shadow()[0]
    assert row.bot["run"] == "dry"
    d = tmp_path / "rows" / str(row.id)
    assert sorted(f.name for f in d.iterdir()) == ["describe.txt", "page-1.jpg", "page-2.jpg"]
    assert browse.drop_pages(tmp_path, row.id) and not d.exists()
    # the pages come from the capture's way down: no second pass back up to the top (#89)
    assert r.shots[0] == b"feed-01" and r.shots[-1] == b"feed-11"
    assert not any(a[0] == "swipe" and a[2] < 0 for a in r.phone.actions)
    assert browse.today(r.store, r.clock(), utc_day) == {"profiles": 1, "skips": 1, "likes": 0, "would_like": 0}


def test_dry_holds_a_like_for_dev_and_waits(tmp_path):
    r = Rig(tmp_path, like())
    r.b.step()
    assert r.holds == [(browse.HOLD_WOULD_LIKE, 1)] and r.kinds() == [likes.WOULD_LIKE]
    assert r.phone.sent() == [] and not any(a[0] == "type" for a in r.phone.actions)
    assert r.phone.screen != "sheet"                                   # no panel opened
    n = len(r.phone.actions)
    r.b.step()
    assert len(r.phone.actions) == n and r.b.state.state == "needs_you"   # paused until Dev resumes
    assert r.b.state.reason == browse.HOLD_WOULD_LIKE                     # the panel says why


@pytest.mark.parametrize("bot,kind,sent", [
    (like(text="Wo ist dieser See?"), likes.LIKE, ["Send like"]),
    (like(comment="none", missing="x"), likes.LIKE_PLAIN, ["Send like"]),
])
def test_live_likes_with_or_without_a_comment(tmp_path, bot, kind, sent):
    r = Rig(tmp_path, bot, mode=browse.LIVE)
    r.b.step()
    assert r.kinds() == [kind] and [a[2] for a in r.phone.sent()] == sent
    typed = [a[2] for a in r.phone.actions if a[0] == "type"]
    assert typed == (["Wo ist dieser See?"] if kind == likes.LIKE else [])
    assert r.store.shadow()[0].bot["run"] == "live" and not (tmp_path / "rows").exists()
    # the like is kept for its chat (#77): her profile as text, our comment, the liked section's kind
    (c,) = r.store.liked("Alex")
    assert c.state == "liked" and c.like_comment == (typed[0] if typed else "") and c.liked_kind == "photo"
    assert c.profile and "Alex" not in c.profile and c.started == r.clock()


def test_dry_and_skips_keep_no_liked_chat(tmp_path):
    for bot, mode in ((like(), browse.DRY), (SKIP, browse.LIVE)):
        r = Rig(tmp_path, bot, mode=mode)
        r.b.step()
        assert r.store.chats() == []


def test_live_stops_before_the_budget(tmp_path):
    r = Rig(tmp_path, like(), mode=browse.LIVE, daily_likes=2)
    for _ in range(2):
        r.b.step()
        r.phone.renamed = False   # the next profile: same dumps
    assert r.kinds().count(likes.LIKE) == 2
    n = len(r.phone.actions)
    assert r.b.step() > 0 and len(r.phone.actions) == n and r.b.state.state == "done_today"
    assert r.notes and "likes used up" in r.notes[0]
    r.b.step()
    assert len(r.notes) == 1                                          # told once per batch


def test_outside_the_window_waits_untouched(tmp_path):
    r = Rig(tmp_path, SKIP, now=T0 + 8 * H)
    assert r.b.step() == 600.0 and r.b.state.until == T0 + 10 * H and r.phone.actions == []
    r.clock.t = T0 + 18 * H
    r.b.step()
    assert r.b.state.until == T0 + DAY + 10 * H and r.phone.actions == []


def test_another_profile_after_the_decision_holds_without_a_tap(tmp_path):
    r = Rig(tmp_path, SKIP)

    def decide(p, lib):
        r.phone.renamed = True   # the app swapped the profile while deciding (#154)
        return FakeDecider(SKIP).decide(p, lib)
    r.decider.decide = decide
    r.b.step()
    assert r.holds == [(browse.HOLD_SCREEN, 1)] and r.phone.sent() == []


def test_paywall_keeps_the_panel_and_sends_once_the_likes_are_back(tmp_path):
    r = Rig(tmp_path, like(), mode=browse.LIVE)
    r.store.add_action(likes.LIKE, "live", ts=T0 - 12 * H + 1)   # the one like in the window: back at 12:00 + 1 s
    r.clock.t = T0 + 11 * H
    r.phone.out_of_likes = True
    r.b.step()
    assert likes.OUT_OF_LIKES in r.kinds() and r.phone.sent() == [] and r.b.pending
    r.b.step()
    assert r.b.state.state == "done_today" and r.b.state.until == T0 + 12 * H + 1
    r.phone.out_of_likes = False
    r.clock.t = T0 + 12 * H + 2
    r.b.step()
    assert [a[2] for a in r.phone.sent()] == ["Send like"] and r.kinds()[-2:] == [likes.LIKE, likes.LIKES_BACK]
    assert len(r.store.liked("Alex")) == 1   # the like the paywall held still leaves its chat


def test_decide_errors_retry_then_hold(tmp_path):
    r = Rig(tmp_path, SKIP)

    def fail(p, lib):
        raise DecideError("timeout")
    r.decider.decide = fail
    assert r.b.step() == 60.0 and r.b.step() == 60.0 and not r.holds
    r.b.step()
    assert r.holds == [(browse.HOLD_DECIDE, None)] and r.whys == ["last: DecideError: timeout"]


def test_usage_limit_waits(tmp_path):
    r = Rig(tmp_path, SKIP)

    def limited(p, lib):
        raise UsageLimited(r.clock() + 3 * H)
    r.decider.decide = limited
    assert r.b.step() == 600.0 and r.b.state.state == "limit" and not r.holds and r.phone.sent() == []


def test_random_breaks(tmp_path):
    r = Rig(tmp_path, SKIP, break_every=(2, 2), break_min=(5, 5))
    for _ in range(2):
        r.b.step()
        r.phone.renamed = False
    assert likes.BREAK in r.kinds()
    n = len(r.phone.actions)
    assert r.b.step() == 300.0 and r.b.state.state == "break" and len(r.phone.actions) == n


def test_parse_range():
    assert browse.parse_range("10-18", "X") == (10.0, 18.0)
    with pytest.raises(ValueError):
        browse.parse_range("18-10", "X")


def test_rows_lists_unlabelled_dry_rows(tmp_path):
    from buddy_screener.main import dry_rows
    r = Rig(tmp_path, SKIP)
    r.b.step()
    out = dry_rows(r.store, tmp_path)
    assert out[0].startswith("#1 ") and "bot skip: 2 pages in" in out[0] and any("reason: generic" in x for x in out)
    r.store.label(1, {"decision": "skip"})
    assert dry_rows(r.store, tmp_path) == ["no unlabelled dry rows"]


def test_run_steps_the_chats_first_and_waits_the_shorter(tmp_path):
    r = Rig(tmp_path, SKIP, mode=browse.OFF)
    order, waits, turns = [], [], iter([True, True, False])

    def chats():
        order.append("chats")
        return 42.0
    step = r.b.step
    r.b.step = lambda: (order.append("browse"), step())[1]
    r.b.run(lambda: next(turns), waits.append, first=chats)
    assert order == ["chats", "browse"] * 2 and waits == [42.0, 42.0]


def test_run_turns_the_screen_off_once_it_goes_idle(tmp_path):
    # #90: the phone stays awake while charging; the worker turns it off after using it
    r = Rig(tmp_path, SKIP, mode=browse.OFF)
    offs = lambda: sum(a[0] == "screen_off" for a in r.phone.actions)
    r.b.run(iter([True, False]).__next__, lambda s: None, first=lambda: 42.0)
    assert offs() == 0                                   # never woken by us: left as it is
    r.b.nav.wake()
    r.b.run(iter([True, False]).__next__, lambda s: None, first=lambda: 42.0)
    assert offs() == 0                                   # a short wait (a gap between profiles): stays on
    r.b.run(iter([True, True, False]).__next__, lambda s: None, first=lambda: 600.0)
    assert offs() == 1 and not r.b.nav.awake             # idle: off once, not again


def test_run_turns_the_screen_off_for_a_hold_but_not_while_paused(tmp_path):
    r = Rig(tmp_path, SKIP, mode=browse.OFF)
    offs = lambda: sum(a[0] == "screen_off" for a in r.phone.actions)
    r.b.nav.wake()
    r.b.mode = lambda: "paused"                          # Dev uses the phone
    r.b.run(iter([True, False]).__next__, lambda s: None, first=lambda: 600.0)
    assert offs() == 0
    r.b.mode = lambda: "needs_you"                       # held: Dev answers on Telegram
    r.b.run(iter([True, False]).__next__, lambda s: None, first=lambda: 5.0)
    assert offs() == 1
