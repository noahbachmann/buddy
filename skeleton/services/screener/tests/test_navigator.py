import pytest

from buddy_screener import navigator, screens
from buddy_screener import vocab as v
from buddy_screener.device import Stopped
from buddy_screener.navigator import Navigator, NeedsYou
from buddy_screener.profile import Capture
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
    # the detail row once, where it's whole (feed-02 cuts it at the bottom, feed-04 at the top): to its end and back
    assert [(a[1], a[2] > 0) for a in phone.actions if a[0] == "swipe_h"] == [("feed-03", True), ("feed-03", False)]


def test_screenshots_from_the_top_to_the_end_tap_nothing():
    phone = FakePhone("feed-05")
    phone.screencap = lambda half=False: phone.screen.encode()   # which screen was shot
    shots = nav(phone).screenshots()
    assert shots[0] == b"feed-01" and len(shots) == 11 and phone.screen == "feed-11"   # each fixture one swipe on
    assert [a for a in phone.actions if a[0] != "swipe"] == []
    assert all(a[2] > 600 for a in phone.actions if a[0] == "swipe" and a[2] > 0)   # a screen per swipe, not STEP


def test_shot_pages_four_screens_side_by_side():
    import io

    from PIL import Image

    from buddy_screener.imaging import shot_pages

    pages = shot_pages([Image.new("RGB", (540, 1140), "gray")] * 5)   # half size, as taken
    sizes = [Image.open(io.BytesIO(p)).size for p in pages]
    assert sizes == [(4 * 540 + 3 * 10, 1095), (540, 1095)]


def test_capture_scrolls_back_to_the_top_first():
    phone = FakePhone("feed-05")
    cap = nav(phone).capture_profile()
    assert cap.profile.sections[0].ref == "photo 1" and cap.profile.sections[0].complete


def test_swipes_start_beside_the_cards():
    phone = FakePhone("feed-01")
    nav(phone).capture_profile()
    xs = {a[3] for a in phone.actions if a[0] == "swipe"}
    assert xs == {1050}   # cards end at 1020, the list at 1080: a swipe on a video's player gets swallowed


def test_one_swallowed_swipe_is_not_the_profiles_end():
    phone = FakePhone("feed-05")
    n = nav(phone)
    cap = n.capture_profile()
    clean = len(cap.profile.sections)
    phone2 = FakePhone("feed-01")
    orig = phone2.swipe
    def swipe(x, y1, y2, ms):   # the 3rd swipe down is taken by a video
        if y2 < y1 and sum(a[0] == "swipe" for a in phone2.actions) == 2:
            phone2.swallow = 1
        orig(x, y1, y2, ms)
    phone2.swipe = swipe
    cap2 = nav(phone2).capture_profile()
    assert phone2.screen == "feed-11" and len(cap2.profile.sections) == clean


@pytest.mark.parametrize("video", [["feed-06"], ["feed-06", "feed-07"]])
def test_capture_swipes_past_a_playing_video(video):
    clean = nav(FakePhone("feed-01")).capture_profile().profile.sections
    phone = FakePhone("feed-01")
    phone.moving = set(video)
    cap = nav(phone).capture_profile()
    assert phone.screen == "feed-11"
    # what the gap hid is lost or cut (incomplete: never liked); every complete section is a clean one, in place
    # (the fixtures' steps are ~900 px: the gap is stitched by the swipes' measured scale, not by anchors)
    done = [x for x in cap.profile.sections if x.complete]
    for x in done:
        assert any(c.kind == x.kind and c.text == x.text and abs(c.lo - x.lo) <= 40 and abs(c.hi - x.hi) <= 40
                   for c in clean), (x, clean)
    assert len(done) >= len(clean) - 1 - len(video)
    assert all(x.complete for x in cap.profile.sections[-2:])   # after the gap: tracked again


def test_capture_ending_on_a_video_backs_up_to_a_readable_screen():
    clean = nav(FakePhone("feed-01")).capture_profile()
    phone = FakePhone("feed-01")
    phone.moving = {"feed-11"}
    n = nav(phone)
    cap = n.capture_profile()
    assert phone.screen == "feed-10"
    assert cap.unmoved(screens.profile_view(n.stable().root))   # the next command trusts the offset
    assert [x.ref for x in cap.profile.sections] == [x.ref for x in clean.profile.sections]


def test_a_command_that_starts_on_a_playing_video_swipes_up_first():
    phone = FakePhone("feed-06")
    phone.moving = {"feed-06"}
    cap = nav(phone).capture_profile()
    assert phone.actions[0] == ("swipe", "feed-06", -600, 1050)   # towards the top, beside the cards
    assert phone.screen == "feed-11" and cap.profile.sections[0].ref == "photo 1"


def test_scroll_to_crosses_a_playing_video(monkeypatch):
    # the fake moves one dump per swipe whatever its length (~900 px), the phone moves by the swipe: same length as
    # the capture's swipes, so the measured scale holds for the blind gap
    monkeypatch.setattr(navigator, "MAX_MOVE", navigator.STEP)
    phone = FakePhone("feed-01")
    phone.moving = {"feed-06"}
    n = nav(phone)
    cap = n.capture_profile()
    s, sv = n.scroll_to(cap, "photo 1")
    assert phone.screen == "feed-01" and sv.kind == "photo"
    assert phone.sent() == []


def test_a_video_that_never_leaves_the_screen_needs_you(tmp_path):
    phone = FakePhone("feed-03")
    phone.moving = {"feed-01", "feed-02"}    # scrolling up hits the video, and the top is under it
    with pytest.raises(NeedsYou, match="unreadable"):
        nav(phone, tmp_path).capture_profile()


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
    c = n.open_chat(n.matches()[0].key)
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


def test_detail_row_swiped_to_its_end_and_back():
    phone = FakePhone("row-0")
    n = nav(phone)
    s, view = n._profile()
    cap = Capture(view)
    seen, done = [], []
    n._explore_rows(cap, s, view, seen, done)
    labels = {label for label, _ in cap.profile.details}
    assert {"Age", "Gender", "Height", "Location", "Children", "Family Plans", "Zodiac Sign"} <= labels
    unlabelled = [value for label, value in cap.profile.details if not label]
    assert unlabelled == ["Text 5"]       # row values all got their label; Text 5 sits below the row, no icon label
    assert phone.screen == "row-0"        # back where it started
    assert [a[0] for a in phone.actions] == ["swipe_h"] * 4 + ["swipe_h"]
    n._explore_rows(cap, s, view, seen, done)   # same row again: nothing
    assert len(phone.actions) == 5 and phone.sent() == []


def test_scroll_to_relocates_after_dev_scrolled():
    phone = FakePhone("feed-01")
    n = nav(phone)
    cap = n.capture_profile()
    phone.screen = "feed-05"                  # Dev scrolled up meanwhile: shares nothing with the last dump
    s, sv = n.scroll_to(cap, "photo 1")
    assert sv.kind == "photo" and phone.screen == "feed-01"
    assert phone.sent() == []


def test_a_playing_videos_clock_doesnt_make_the_screen_unstable():
    from buddy_screener.uitree import parse
    xml = '<hierarchy><node class="android.view.View" content-desc="Elapsed time: {} seconds" bounds="[0,0][9,9]"/></hierarchy>'
    assert navigator._signature(parse(xml.format(7))) == navigator._signature(parse(xml.format(8)))


POLL_WALK = [f"feed-poll-off-{i}" for i in range(1, 4)]


def poll_capture(dumps: int = 3):
    from buddy_screener.uitree import parse
    from fixture_files import load
    v = [screens.profile_view(parse(load(f))) for f in POLL_WALK[:dumps]]
    cap = Capture(v[0])
    for x in v[1:]:
        cap.add(x, expect=cap.offset + 600)
    return cap


def test_scroll_to_a_poll_option_brings_its_heart_up():
    phone = FakePhone("feed-poll-off-3", walk=POLL_WALK)
    n = nav(phone)
    s, sv = n.scroll_to(poll_capture(), "option 3")
    assert (sv.kind, sv.text) == ("option", "Option 3") and screens.tappable(s, sv.like)
    assert phone.sent() == []


def test_an_option_under_another_polls_question_needs_you():
    phone = FakePhone("feed-poll-off-2", walk=POLL_WALK)   # this dump still shows the question
    n = nav(phone)
    cap = poll_capture(2)
    cap.profile.poll_of(cap.profile.find("option 1")).text = "Another question"
    with pytest.raises(NeedsYou, match="another poll's question"):
        n.scroll_to(cap, "option 1")


def _option(s, text):
    return next(x for x in screens.profile_view(s.root).sections if x.kind == "option" and x.text == text)


def test_open_a_poll_options_panel_type_and_back_out():
    phone = FakePhone("feed-poll-all", walk=["feed-poll-all"])
    n = nav(phone)
    s = n.stable()
    n.open_sheet(s, _option(s, "Option 2"))
    assert phone.screen == "sheet-option-typed"   # scrolled clear of the skip button first
    n.type_comment("wo gosch am liebste go wandere?")
    assert phone.field == "wo gosch am liebste go wandere?"
    s, _ = n.back_out("Alex")
    assert s.kind is Kind.FEED and phone.field is None
    assert phone.sent() == []


def test_a_panel_open_for_another_option_needs_you():
    phone = FakePhone("feed-poll-all", walk=["feed-poll-all"])
    n = nav(phone)
    s = n.stable()
    with pytest.raises(NeedsYou, match="another section"):
        n.open_sheet(s, _option(s, "Option 3"))   # the app picked option 2
    assert phone.sent() == [] and not any(a[0] == "type" for a in phone.actions)


FRIEND_WALK = [f"feed-friend-{i}" for i in range(1, 6)]


def test_capture_starts_swipes_off_the_friends_carousel():
    # #165: the carousel filled the list's bottom where swipes started; it took them sideways, nothing moved twice,
    # and the capture ended there as if at the profile's bottom (photo 1 and two friends' cards only)
    phone = FakePhone("feed-friend-1", walk=FRIEND_WALK)
    cap = nav(phone).capture_profile()
    refs = [s.ref for s in cap.profile.sections]
    assert phone.screen == "feed-friend-5" and refs[:4] == ["photo 1", "friend 1", "friend 2", "poll 1"]
    assert refs[-1] == "prompt 1"
    assert phone.sent() == []
