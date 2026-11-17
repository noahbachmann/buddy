import pytest

from buddy_screener import screens
from buddy_screener import vocab as v
from buddy_screener.device import Stopped
from buddy_screener.navigator import Navigator, NeedsYou
from buddy_screener.screens import Kind
from fake_phone import FakePhone
from fixture_files import PACKAGE


def nav(phone: FakePhone, tmp_path=None) -> Navigator:
    return Navigator(phone, PACKAGE, dump_dir=tmp_path, pause=lambda s: None, log=lambda s: None)


def test_capture_whole_profile_and_stop_at_the_end():
    phone = FakePhone("feed-01")
    cap = nav(phone).capture_profile()
    assert len(cap.profile.sections) == 9
    assert phone.screen == "feed-11"
    assert phone.sent() == []


def test_capture_scrolls_back_to_the_top_first():
    phone = FakePhone("feed-05")
    cap = nav(phone).capture_profile()
    assert cap.profile.sections[0].ref == "photo 1" and cap.profile.sections[0].complete


def test_scroll_to_a_section_open_its_sheet_type_and_back_out():
    phone = FakePhone("feed-01")
    n = nav(phone)
    cap = n.capture_profile()
    s, sv = n.scroll_to(cap, "photo 1")      # the fake has the sheet fixture for photo 1 only
    assert phone.screen == "feed-01" and sv.kind == "photo"
    n.open_sheet(s, sv)
    n.type_comment("Test äöü ß – ok")
    assert phone.field == "Test äöü ß – ok"
    s, same = n.back_out(cap.profile.name)
    assert s.kind is Kind.FEED and same and phone.field is None
    assert [a[0] for a in phone.actions[-4:]] == ["clear", "key", "key", "launch"]
    assert phone.sent() == []


def test_low_panel_is_scrolled_clear_before_any_tap():
    phone = FakePhone("sheet-low")
    n = nav(phone)
    s = n._sheet_clear(n.stable())
    assert phone.screen == "sheet" and s.kind is Kind.SHEET
    assert [a[0] for a in phone.actions] == ["swipe"] and phone.actions[0][2] > 0   # content moved up


def test_text_that_did_not_land_needs_you(tmp_path):
    phone = FakePhone("sheet")
    n = nav(phone, tmp_path)
    phone.tap = lambda target: (0, 0)        # the field never gets focus
    with pytest.raises(NeedsYou) as e:
        n.type_comment("hello")
    assert "didn't land" in e.value.reason
    assert e.value.dump is not None and e.value.dump.exists()   # kept for a fixture


def test_tabs_matches_chat_and_back():
    phone = FakePhone("feed-01")
    n = nav(phone)
    assert n.go_tab(v.TAB_LIKES).kind is Kind.LIKES
    n.go_tab(v.TAB_MATCHES)
    c = n.open_chat(0)
    assert c.kind is Kind.CHAT
    assert n.go_tab(v.TAB_DISCOVER).kind is Kind.FEED   # leaves the chat by its back button first
    assert ("tap", "chat", "Back") in phone.actions


def test_unknown_screen_stops_without_input():
    phone = FakePhone("feed-01")
    phone.dump = lambda: "<?xml version='1.0' ?><hierarchy><node package=\"app.example\" class=\"x.View\" " \
                         "bounds=\"[0,0][1080,2280]\" /></hierarchy>"
    with pytest.raises(NeedsYou):
        nav(phone).go_tab(v.TAB_MATCHES)
    assert phone.actions == []


def test_popup_in_front_is_closed():
    phone = FakePhone("popup")
    s = nav(phone).go_tab(v.TAB_DISCOVER)
    assert s.kind is Kind.FEED
    assert phone.actions == [("tap", "popup", "Close")]


def test_popup_after_launch_is_closed():
    phone = FakePhone("feed-01")
    phone.foreground_pkg, phone.launch_screen = "launcher.example", "popup"
    assert nav(phone).ensure_app().kind is Kind.FEED
    assert [a[0] for a in phone.actions] == ["launch", "tap"]


def test_popups_that_keep_coming_need_you():
    phone = FakePhone("popup")
    phone.popup_sticks = True
    with pytest.raises(NeedsYou):
        nav(phone).go_tab(v.TAB_DISCOVER)
    assert len(phone.actions) == 3


def test_refuses_a_tap_off_the_allowlist():
    phone = FakePhone("sheet")
    n = nav(phone)
    s = n.stable()
    send = s.root.first(lambda x: x.desc == v.SEND_LIKE)
    with pytest.raises(NeedsYou):
        n.tap(s, send, "send")
    assert phone.actions == []


def test_refuses_a_like_button_under_the_tab_bar():
    phone = FakePhone("feed-06")
    n = nav(phone)
    s = n.stable()
    p = screens.profile_view(s.root)
    with pytest.raises(NeedsYou):
        n.tap(s, p.sections[-1].like, "like")
    assert phone.actions == []


def test_kill_switch_stops_the_next_action():
    phone = FakePhone("feed-01")

    def stopped():
        raise Stopped("stopped")
    phone.guard = stopped
    with pytest.raises(Stopped):
        nav(phone).capture_profile()   # at the top already: the first swipe is the first action
    assert phone.actions == []
