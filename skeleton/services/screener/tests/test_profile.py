import pytest

from buddy_screener import screens
from buddy_screener.profile import Capture, LostTrack, dhash, hash_distance, shift_candidates
from buddy_screener.uitree import parse
from fixture_files import FEED, PACKAGE, load


def view(name: str) -> screens.ProfileView:
    return screens.profile_view(screens.classify(parse(load(name)), PACKAGE).root)


def capture(names) -> Capture:
    views = [view(n) for n in names]
    cap = Capture(views[0])
    for v in views[1:]:
        cap.add(v)
    return cap


def test_whole_profile_from_the_real_walk():
    cap = capture(FEED)
    p = cap.profile
    assert [s.ref for s in p.sections] == [
        "photo 1", "prompt 1", "photo 2", "photo 3", "prompt 2", "photo 4", "prompt 3", "photo 5", "photo 6"]
    assert all(s.complete for s in p.sections)
    # sections don't overlap: at least the app's 60 px gap (the details block sits after prompt 1)
    for a, b in zip(p.sections, p.sections[1:]):
        assert b.top - a.bottom >= 60
    assert all(s.bottom - s.top == 960 for s in p.sections if s.kind == "photo")
    assert len({s.text for s in p.sections if s.kind == "prompt"}) == 3
    assert [label for label, _ in p.details][:3] == ["Age", "Gender", "Sexuality"]


def test_offsets_are_measured_not_assumed():
    # the first swipe also collapsed the header: 625 px, the others ~900
    cap = capture(FEED[:3])
    assert cap.offset == 1536


def test_scrolling_back_up_finds_the_same_sections():
    cap = capture(FEED)
    for name in reversed(FEED[:-1]):
        cap.add(view(name))
    assert cap.offset == 0
    assert len(cap.profile.sections) == 9


def test_photos_tie_broken_by_the_expected_swipe():
    a, b = view("feed-04"), view("feed-05")
    cands = shift_candidates(a, 2440, b, expect=2440 + 900)
    assert cands[0] == 3347


def test_other_profile_loses_track():
    cap = capture(FEED[:2])
    other = view("feed-03")
    other.name = "Someone else"
    with pytest.raises(RuntimeError):
        cap.add(other)


def test_dhash_and_distance():
    flat = [[10] * 9 for _ in range(8)]
    ramp = [[x * 10 for x in range(9, 0, -1)] for _ in range(8)]
    assert dhash(flat) == "0000000000000000"
    assert dhash(ramp) == "ffffffffffffffff"
    assert hash_distance(dhash(flat), dhash(ramp)) == 64


def test_voice_prompt_is_a_section_and_an_anchor():
    # 2026-11-20: video prompt, a feature ad, then a voice prompt; unknown, the voice card left no anchor: lost track
    cap = capture([f"feed-voice-{i}" for i in range(1, 5)])
    kinds = [(s.kind, s.text) for s in cap.profile.sections]
    assert ("voice", "Question 2") in kinds and ("video", "Question 1") in kinds
    assert all(s.like is None for s in view("feed-voice-3").sections if s.kind == "voice")


def test_a_section_taller_than_the_swipe_is_crossed_by_estimate():
    # live 2026-11-20: a tall video prompt, its bottom off screen, then only its bottom: no shared edge
    prev, cur = view("feed-voice-1"), view("feed-voice-2")
    video = next(s for s in prev.sections if s.kind == "video")
    video.bottom_cut = True
    cap = Capture(prev)
    assert abs(cap.add(cur, expect=600) - 620) <= 40   # true shift 620 (video bottom 1967 → 1347)
    assert [s.kind for s in cap.profile.sections].count("video") == 1


def test_no_shared_section_still_loses_track():
    prev, cur = view("feed-voice-1"), view("feed-voice-4")
    with pytest.raises(LostTrack):
        Capture(prev).add(cur, expect=600)
