import pytest

from buddy_screener import screens
from buddy_screener import vocab as v
from buddy_screener.screens import Kind
from buddy_screener.uitree import Rect, parse
from fixture_files import FEED, PACKAGE, load


def screen(name: str) -> screens.Screen:
    return screens.classify(parse(load(name)), PACKAGE)


@pytest.mark.parametrize("name, kind", [
    *[(f, Kind.FEED) for f in FEED],
    ("sheet", Kind.SHEET), ("sheet-typed", Kind.SHEET),
    ("likes", Kind.LIKES), ("matches", Kind.MATCHES), ("chat", Kind.CHAT), ("popup", Kind.POPUP),
])
def test_every_fixture_is_recognised(name, kind):
    assert screen(name).kind is kind


def test_own_profile_tab_is_unknown():
    s = screen("own-profile")
    assert s.kind is Kind.UNKNOWN and s.selected_tab == v.TAB_PROFILE


def test_panel_under_the_skip_button_is_not_tappable():
    s = screen("sheet-low")
    assert s.kind is Kind.SHEET
    p, panel = screens.profile_view(s.root), screens.sheet_view(s.root)
    assert panel.section.kind == "prompt"
    assert not p.safe(panel.field.bounds)


def test_another_package_is_other_app():
    assert screens.classify(parse(load("feed-01")), "some.other").kind is Kind.OTHER_APP


def test_feed_on_another_tab_is_unknown():
    # a profile under another tab (an incoming like's card: layout unseen) must not pass as the feed
    xml = load("feed-02")
    for tab in ("Discover", "Likes You"):
        xml = xml.replace(f'content-desc="{tab}"', f'content-desc="{tab}-x"')
    xml = xml.replace('content-desc="Likes You-x"', 'content-desc="Discover"')
    xml = xml.replace('content-desc="Discover-x"', 'content-desc="Likes You"')
    s = screens.classify(parse(xml), PACKAGE)
    assert s.kind is Kind.UNKNOWN


def test_tabs_and_badge():
    s = screen("matches")
    assert s.selected_tab == v.TAB_MATCHES
    assert s.tabs[v.TAB_MATCHES].badge == 1
    assert not s.tabs[v.TAB_DISCOVER].selected


def test_profile_sections_and_like_buttons():
    p = screens.profile_view(screen("feed-02").root)
    assert p.name == "Alex"
    assert [s.kind for s in p.sections] == ["photo", "prompt"]
    photo, prompt = p.sections
    assert photo.top_cut and not photo.bottom_cut
    assert prompt.text.startswith("Question") and "\n" in prompt.text
    assert photo.like.desc == v.LIKE_PHOTO and prompt.like.desc == v.LIKE_PROMPT
    assert p.safe(prompt.like.bounds)


def test_like_button_sticking_out_of_the_list_is_not_safe():
    # feed-06: the prompt is cut at the bottom and its like button lies over the tab bar
    p = screens.profile_view(screen("feed-06").root)
    prompt = p.sections[-1]
    assert prompt.kind == "prompt" and prompt.bottom_cut
    assert prompt.like is not None and not p.safe(prompt.like.bounds)


def test_skip_button_is_an_obstacle():
    p = screens.profile_view(screen("feed-02").root)
    skip = Rect(60, 1724, 246, 1910)
    assert skip in p.obstacles
    assert not p.safe(Rect(100, 1750, 200, 1800))


def test_list_top_makes_the_first_photo_whole():
    p = screens.profile_view(screen("feed-01").root)
    assert p.at_top and not p.sections[0].top_cut
    assert not screens.profile_view(screen("feed-02").root).at_top


def test_name_with_trailing_space_still_finds_the_top():
    # seen live 2026-11-19: skip button "Skip <name> ", header text "<name>"
    xml = load("feed-01").replace('content-desc="Skip Alex"', 'content-desc="Skip Alex "')
    p = screens.profile_view(screens.classify(parse(xml), PACKAGE).root)
    assert p.name == "Alex" and p.at_top


def test_details_paired_with_the_nearest_label():
    p = screens.profile_view(screen("feed-03").root)
    labels = [label for label, _ in p.details]
    assert labels[:3] == ["Age", "Gender", "Sexuality"]


def test_sheet_belongs_to_the_section_above():
    sv = screens.sheet_view(screen("sheet").root)
    assert sv.section.kind == "photo"
    assert sv.send.desc == v.SEND_LIKE
    assert screens.sheet_view(screen("sheet-typed").root).send.desc == v.SEND_LIKE_WITH


def test_match_rows_skip_the_section_header():
    rows = screens.match_rows(screen("matches").root)
    assert [r.name for r in rows] == ["Sam"]


def test_likes_empty_state():
    assert screens.likes_empty(screen("likes").root)


def test_chat_messages_and_origin():
    c = screens.chat_view(screen("chat").root)
    assert c.name == "Sam"
    assert [(m.side, m.sender) for m in c.messages] == [("left", "Sam")]
    assert c.origin == "You liked Sam's photo."


def test_allowlist_refuses_send_skip_and_paid():
    sheet = screen("sheet")
    root = sheet.root
    send = root.first(lambda n: n.desc == v.SEND_LIKE)
    paid = root.first(lambda n: n.cls == "Button" and n.bounds.left == 108)   # the paid button next to send
    skip = root.first(lambda n: n.desc.startswith("Skip "))
    field = root.first(lambda n: n.desc == v.EDIT_COMMENT)
    assert not screens.tappable(sheet, send)
    assert not screens.tappable(sheet, paid)
    assert not screens.tappable(sheet, skip)
    assert screens.tappable(sheet, field)
    feed = screen("feed-02")
    assert not screens.tappable(feed, feed.root.first(lambda n: n.desc.endswith("'s photo")))
    chat = screen("chat")
    assert not screens.tappable(chat, chat.root.first(lambda n: n.rid == v.CHAT_VOICE))
    assert not screens.tappable(chat, chat.root.first(lambda n: v.MESSAGE.fullmatch(n.desc) is not None))


def test_popup_allows_only_close():
    s = screen("popup")
    close = s.root.first(lambda n: n.desc == v.CLOSE)
    others = [n for n in s.root.iter() if n.clickable and n is not close]   # "try it", "learn more"
    assert screens.tappable(s, close)
    assert others and not any(screens.tappable(s, n) for n in others)


def test_summary_has_no_names():
    for name in ("feed-01", "matches", "chat"):
        assert "Alex" not in screens.summary(screen(name)) and "Sam" not in screens.summary(screen(name))


def test_detail_row_found():
    view = screens.profile_view(screen("row-0").root)
    assert len(view.rows) == 1 and view.safe(view.rows[0])
    assert [label for label, _ in view.details][:3] == ["Age", "Gender", "Height"]


def test_captioned_photo_has_its_like_button():
    s = screen("photo-caption")
    view = screens.profile_view(s.root)
    photo = next(x for x in view.sections if x.kind == "photo")
    assert photo.like is not None and photo.like.desc == v.LIKE_PHOTO_CAPTION
    assert view.safe(photo.like.bounds) and screens.tappable(s, photo.like)


def test_upsell_acts_only_on_the_plain_like():
    s = screen("upsell")
    assert s.kind is Kind.UPSELL
    rose = s.root.first(lambda n: n.text == "Send a Rose").parent
    close = s.root.first(lambda n: n.desc == v.CLOSE_SHEET)
    anyway = screens.send_anyway_button(s.root)
    assert screens.actable(s, anyway)
    assert not screens.actable(s, rose) and not screens.actable(s, close)
    assert not any(screens.tappable(s, n) for n in (anyway, rose, close))   # never through the browse allowlist


def test_acting_allowlist():
    feed, sheet = screen("feed-02"), screen("sheet")
    skip = screens.skip_button(feed.root)
    assert screens.actable(feed, skip) and not screens.tappable(feed, skip)
    send = sheet.root.first(lambda n: n.desc == v.SEND_LIKE)
    paid = sheet.root.first(lambda n: n.cls == "Button" and n.bounds.left == 108)
    assert screens.actable(sheet, send) and not screens.actable(sheet, paid)
    assert not screens.actable(feed, feed.root.first(lambda n: n.desc == v.LIKE_PHOTO))


def test_a_video_is_a_section_without_a_like():
    view = screens.profile_view(screen("feed-video").root)   # real dump, a video between a photo and a prompt
    assert [x.kind for x in view.sections] == ["photo", "video", "prompt"]
    video = view.sections[1]
    assert video.like is None and view.sections[0].like is not None
    assert screen("feed-video").kind is Kind.FEED


def test_a_video_prompt_is_a_video_section_with_its_question():
    s = screen("feed-video-prompt")   # real dump: the profile's top, a video prompt playing under photo 1
    view = screens.profile_view(s.root)
    assert s.kind is Kind.FEED and view.at_top
    assert [(x.kind, x.text) for x in view.sections] == [("photo", ""), ("video", "Question 1")]
    assert view.sections[1].like is None


def test_a_poll_is_a_section_and_feature_ads_are_no_details():
    s = screen("feed-poll")   # real dump: a poll's card (question cut at the top), a feature ad, prompt 1
    view = screens.profile_view(s.root)
    assert [(x.kind, x.text) for x in view.sections] == [
        ("poll", "Question 1"), ("option", "Option 1"), ("option", "Option 2"), ("option", "Option 3"),
        ("prompt", "Question 2\nAnswer 1")]
    poll = view.sections[0]
    assert poll.top_cut and not poll.bottom_cut and poll.like is None and poll.rect.height > 900   # the card
    assert view.details == []   # the ad's texts ("Text 1", "Try it") aren't hers
    assert s.kind is Kind.FEED


def test_out_of_likes_is_no_popup():
    s = screen("out-of-likes")   # real dump: the paywall after "send like" when the day's free likes are gone
    assert s.kind is Kind.OUT_OF_LIKES
    close = screens.close_button(s.root)
    assert screens.tappable(s, close) and not any(screens.tappable(s, n) for n in s.root.iter() if n is not close)


def test_each_poll_option_is_a_section_with_its_heart():
    # #82: an option's heart opens the comment panel for that option; no content-desc on the heart
    s = screen("feed-poll")
    options = [x for x in screens.profile_view(s.root).sections if x.kind == "option"]
    assert [(o.poll, o.rect.top, o.rect.bottom) for o in options] == [
        ("Question 1", 508, 706), ("Question 1", 742, 940), ("Question 1", 976, 1174)]
    assert [(o.like.bounds.left, o.like.bounds.top) for o in options] == [(816, 565), (816, 799), (816, 1033)]
    assert all(screens.tappable(s, o.like) for o in options)
    rows = [o.like.parent for o in options]
    assert not any(screens.tappable(s, r) for r in rows)                       # the row itself: no (it votes?)
    assert not any(screens.tappable(s, n) for r in rows for n in r.children if n is not screens.option_heart(r))


def test_options_after_their_question_scrolled_off_have_no_poll():
    view = screens.profile_view(screen("feed-poll-off-3").root)
    assert [(o.kind, o.poll) for o in view.sections if o.kind == "option"] and \
        all(o.poll == "" for o in view.sections if o.kind == "option")
