"""The navigator's chat moves (#77) on the fake phone walking the chat fixtures."""

import pytest

from buddy_screener import screens
from buddy_screener import vocab as v
from buddy_screener.navigator import Navigator, NeedsYou, same_chat, stitch
from buddy_screener.screens import Kind, Message
from fake_chat_phone import PROFILE, FakeChatPhone
from fixture_files import PACKAGE


def nav(phone) -> Navigator:
    return Navigator(phone, PACKAGE, pause=lambda s: None, log=lambda s: None)


def test_matches_opens_their_turn_and_keys_every_row():
    phone = FakeChatPhone("matches-turns")
    rows = nav(phone).matches()
    assert [r.section for r in rows] == [screens.YOUR_TURN] * 4 + [screens.THEIR_TURN]
    assert len({r.key for r in rows}) == 5
    # on the list's first screen the header sits on the tab bar: tapped once scrolled, and only once
    assert [a for a in phone.actions if a[0] == "tap" and a[2].startswith("Their turn")] == [("tap", "matches-scrolled", "Their turn (1)")]
    assert phone.sent() == []


def test_open_chat_by_key_and_read_it_whole():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Alex")
    msgs, origin = n.read_chat()
    assert [(m.side, m.text) for m in msgs] == [("left", "Message 1")]
    assert origin.kind == "prompt" and origin.comment == "Text 3"
    assert phone.screen == "chat-comment"   # head on the first page: no scrolling
    assert phone.sent() == []


def test_open_chat_refuses_a_missing_row():
    with pytest.raises(NeedsYou, match="gone"):
        nav(FakeChatPhone("matches-turns")).open_chat("Nobody")


def test_chat_profile_captures_her_profile_and_returns_to_the_chat():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Alex")
    cap = n.chat_profile()
    p = cap.profile
    assert p.name == "Alex"
    kinds = [s.kind for s in p.sections]
    assert kinds.count("prompt") == 3 and "video" in kinds and kinds.count("photo") >= 4
    assert dict(p.details).get("Height") and dict(p.details).get("Job")
    assert phone.screen == "chat-comment-top" and n.where is Kind.FEED
    assert any(a[1] == PROFILE[-1] for a in phone.actions)   # read to the end
    assert phone.sent() == []


def test_type_hold_and_send_a_message():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Alex")
    n.type_message("Hi Alex, a few questions")
    assert phone.sent() == [] and n.field_holds("Hi Alex, a few questions")
    n.send_message("Hi Alex, a few questions")
    assert [a[2] for a in phone.sent()] == ["Send"]
    msgs, _ = n.read_chat()
    assert msgs[-1].side == "right" and msgs[-1].text == "Hi Alex, a few questions"


def test_an_edited_field_is_never_sent():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Alex")
    n.type_message("Hi")
    phone.field = "Hi there, it's me"   # Dev changed it on the phone
    assert not n.field_holds("Hi")
    with pytest.raises(NeedsYou, match="doesn't hold"):
        n.send_message("Hi")
    assert phone.sent() == []


def test_typing_into_a_field_with_text_refuses():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Alex")
    phone.field = "half a sentence"
    phone.tap = lambda r, _t=phone.tap: (phone.actions.append(("tap", phone.screen, "Send a message")), (0, 0))[1]
    with pytest.raises(NeedsYou, match="isn't empty"):
        n.type_message("Hi")


def test_unmatch_through_the_menu():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Robin")
    assert n.unmatch().kind is Kind.MATCHES
    assert phone.unmatched == ["chat-photo-comment"]
    assert [a[1] for a in phone.actions if a[0] == "tap"][-2:] == ["chat-photo-comment", "chat-menu"]


def test_leaving_from_the_profile_tab_and_tabs():
    phone = FakeChatPhone("matches-turns")
    n = nav(phone)
    n.open_chat("Sam")
    assert n.leave_chat().kind is Kind.MATCHES


def m(side: str, text: str, media: bool = False) -> Message:
    return Message(side, "x" if side == "left" else "y", text, media)


def test_stitch_joins_on_the_longest_overlap():
    top = [m("left", "a"), m("right", "b"), m("left", "c")]
    bottom = [m("right", "b"), m("left", "c"), m("left", "d")]
    assert [x.text for x in stitch([top, bottom])] == ["a", "b", "c", "d"]


def test_stitch_keeps_a_repeated_text_that_isnt_overlap():
    # "haha" twice in a row: the second page repeats only the last one
    top = [m("left", "q"), m("left", "haha")]
    bottom = [m("left", "haha"), m("left", "haha"), m("right", "ok")]
    assert [x.text for x in stitch([top, bottom])] == ["q", "haha", "haha", "ok"]


def test_stitch_without_overlap_appends():
    assert [x.text for x in stitch([[m("left", "a")], [m("left", "b")]])] == ["a", "b"]


def test_turn_header_is_tappable_menu_rows_are_not():
    from fixture_files import load
    from buddy_screener.uitree import parse
    s = screens.classify(parse(load("matches-turns")), PACKAGE)
    their = [h for h in screens.turn_headers(s.root) if h.section == screens.THEIR_TURN][0]
    assert screens.tappable(s, their.node)
    assert v.MENU_UNMATCH in v.MENU_ITEMS


def test_same_chat_allows_a_few_bits_of_thumbnail_drift():
    assert same_chat("Alex#00ff", "Alex#00ff") and same_chat("Alex#00ff", "Alex#00fe")
    assert not same_chat("Alex#00ff", "Sam#00ff") and not same_chat("Alex#0000", "Alex#ffff")
    assert same_chat("Alex", "Alex") and not same_chat("Alex", "Alex#00ff")


def test_a_closed_their_turn_on_a_short_list_is_opened_at_its_end():
    # the list doesn't scroll (only the header on the tab bar): opened at the end of the pass anyway
    phone = FakeChatPhone("matches-turns")
    phone.swipe = lambda x, y1, y2, ms: phone.actions.append(("swipe", phone.screen, y1 - y2, x))
    rows = nav(phone).matches()
    assert [a for a in phone.actions if a[0] == "tap"] == [("tap", "matches-turns", "Their turn (1)")]
    assert rows[-1].section == screens.THEIR_TURN


def test_a_dark_screen_never_keys_the_matches():
    # screen off: dumps still read, screenshots are black → every thumbnail hash 0 (2026-11-25, the give-back missed)
    phone = FakeChatPhone("matches-turns")
    phone.screencap = lambda half=False: "black"
    n = Navigator(phone, PACKAGE, pause=lambda s: None, log=lambda s: None, hasher=lambda shot, rect: "0" * 16)
    with pytest.raises(NeedsYou, match="dark"):
        n.matches()
    n.hasher = lambda shot, rect: "f0e68ebff9caccf0"
    assert all(r.key.endswith("#f0e68ebff9caccf0") for r in n.matches())
