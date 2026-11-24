"""SQLite store: the chats table and its retention (#77)."""

from buddy_screener.chat import CLOSED, HANDOVER, LIKED, NEW, SCREENING, Chat, Sent
from buddy_screener.store import KEEP_DAYS, Store

DAY = 86400.0


def test_chat_round_trip_and_update(tmp_path):
    db = tmp_path / "s.db"
    s = Store(db, clock=lambda: 100.0)
    c = Chat("k1", "Alex", 50.0, state=SCREENING, answers={1: "long-term"}, sent=[Sent(60.0, "disclosure", "Hi")])
    s.save_chat(c)
    c.turns = 2
    s.save_chat(c)
    back = Store(db, clock=lambda: 100.0).chat("k1")
    assert back == c and back.answers == {1: "long-term"} and back.sent[0].kind == "disclosure"
    assert s.chat("nope") is None
    assert [x.key for x in s.chats((SCREENING,))] == ["k1"] and s.chats((CLOSED,)) == []


def test_ended_chats_go_after_keep_days_open_ones_stay(tmp_path):
    db = tmp_path / "s.db"
    s = Store(db, clock=lambda: 0.0)
    s.save_chat(Chat("open", "A", 0.0, state=SCREENING))
    s.save_chat(Chat("closed", "B", 0.0, state=CLOSED, closed_at=10 * DAY))
    s.save_chat(Chat("handover", "C", 0.0, state=HANDOVER, closed_at=1 * DAY))
    later = Store(db, clock=lambda: (KEEP_DAYS + 5) * DAY)
    assert sorted(c.key for c in later.chats()) == ["closed", "open"]
    much_later = Store(db, clock=lambda: (KEEP_DAYS + 11) * DAY)
    assert [c.key for c in much_later.chats()] == ["open"]


def test_a_like_that_never_matched_goes_after_keep_days(tmp_path):
    db = tmp_path / "s.db"
    s = Store(db, clock=lambda: 0.0)
    s.save_chat(Chat("liked:a", "Alex", 0.0, state=LIKED, like_comment="Hi", liked_kind="prompt"))
    s.save_chat(Chat("liked:b", "Sam", 0.0, state=LIKED))
    assert [c.key for c in s.liked("Alex")] == ["liked:a"] and s.liked("Kim") == []
    linked = s.liked("Sam")[0]   # matched: linked to its chat's key, open from now on
    s.drop_chat(linked.key)
    linked.key, linked.state = "Sam#abc", NEW
    s.save_chat(linked)
    later = Store(db, clock=lambda: (KEEP_DAYS + 1) * DAY)
    assert [c.key for c in later.chats()] == ["Sam#abc"]


def test_live_and_sim_spend_apart():
    from buddy_screener.decide import Usage
    s = Store(":memory:")
    s.add_usage(10.0, Usage("browse", "m", cost=1.0))
    s.add_usage(11.0, Usage("sim-converse", "m", cost=0.25))
    assert s.cost_since(0) == 1.0 and s.cost_since(0, sim=True) == 0.25
