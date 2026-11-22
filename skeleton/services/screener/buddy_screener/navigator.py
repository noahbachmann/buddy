"""Moving around the app on top of `device` and `screens`: tabs, a profile's sections, the comment sheet, a chat.

Rules every move follows:
- act only on a screen that is stable over two dumps (layout can move between a dump and a tap: seen in step 1);
- tap only what `screens.tappable` allows, and on a profile only targets fully inside the list and clear of the
  floating buttons;
- an unknown screen or anything unexpected raises `NeedsYou` (with the dump kept in tmpfs for a fixture), never a
  blind retry.

Nothing here sends anything: no send like, no skip, no chat message (step 3+ adds those, behind their own checks).
"""

import random
import time
from collections.abc import Callable
from typing import Any
from dataclasses import dataclass, field
from pathlib import Path

from . import screens
from . import vocab as v
from .device import KEY_BACK, Device, NotIdle
from .profile import Capture, LostTrack, Section, hash_distance
from .screens import Kind, ProfileView, Screen, SectionView
from .uitree import Node, Rect, parse

STEP = 600            # px per scroll swipe: consecutive dumps share sections (a photo is 960 px)
MAX_MOVE = 700        # px, longest swipe when scrolling to a section: longer ones left no anchor in common
SWIPE_MS = 900        # slow: scrolls by about the swipe length, no fling
MAX_SWIPES = 30
SHOT_OVERLAP = 250   # px a screenshot shares with the one before (a swipe starts 150 px above the list's bottom)
MIN_MARGIN = 30       # px beside the cards for a swipe to start in
BLIND_EDGE = 30       # px from the screen's right edge: beside the cards (they end 60 px before it)
MAX_BLIND = 4        # swipes past an unreadable screen: a video is about one screen high
MAX_POPUPS = 3        # closed in a row before asking Dev
ROW_EDGE = 100        # px from a detail row's ends where a sideways swipe starts/ends
ROW_FLING_MS = 150    # fast: flings the row back to its start in one go (measured)
MAX_ROW_SWIPES = 8
ROW_SAME = 100        # px of content y: the same row in another dump
HASH_MAX_DIST = 10    # of 64 bits: same photo
Hasher = Callable[[Any, Rect], str]   # (screenshot, crop) → dhash
Cropper = Callable[[Any, Rect], bytes]   # (screenshot, crop) → JPEG for the decision call


class _Unreadable(Exception):
    """Still no dump after MAX_BLIND swipes past a moving screen."""


class NeedsYou(RuntimeError):
    """Stop and ask Dev: unknown screen, unexpected state, a check failed."""

    def __init__(self, reason: str, dump: Path | None = None):
        super().__init__(reason + (f" (dump: {dump})" if dump else ""))
        self.reason, self.dump = reason, dump


_rng = random.Random()


def human_pause(seconds: float) -> None:
    """Waits like a person, not a metronome: a bit longer than asked, never the same, now and then a longer look."""
    extra = _rng.uniform(0.5, 2.0) if _rng.random() < 0.1 else 0.0
    time.sleep(seconds * _rng.uniform(0.9, 1.6) + extra)


def _margin_x(view: ProfileView) -> int:
    """x of the gap between the cards' right edge and the list's, else the list's middle column."""
    vp = view.viewport
    edge = max((sv.rect.right for sv in view.sections), default=vp.right)
    return (edge + vp.right) // 2 if vp.right - edge >= MIN_MARGIN else vp.center[0]


def _signature(root: Node) -> tuple:
    """What a still screen keeps between dumps; a playing video's clock is left out (it ticks every second)."""
    return tuple((n.cls, n.desc, n.text, n.rid, n.bounds, n.selected) for n in root.iter()
                 if not v.ELAPSED.fullmatch(n.desc))


@dataclass
class Navigator:
    dev: Device
    package: str
    dump_dir: Path | None = None              # where an unexpected screen's raw dump goes (tmpfs)
    hasher: Hasher | None = None
    cropper: Cropper | None = None            # set: photos keep a JPEG crop (memory only) for the decision
    check_cropper: Cropper | None = None      # set: and a larger one for the photo check (#83)
    pause: Callable[[float], object] = human_pause
    log: Callable[[str], None] = print
    last: Screen | None = field(default=None, init=False)
    _xml: str = field(default="", init=False)
    _earlier: list[str] = field(default_factory=list, init=False)   # the last distinct dumps before `_xml`

    # --- reading the screen ---

    @property
    def xml(self) -> str:
        """The last raw dump (for `--cmd dump`; personal data: tmpfs only)."""
        return self._xml

    def screen(self) -> Screen:
        xml = self.dev.dump()
        if self._xml and xml != self._xml:
            self._earlier = (self._earlier + [self._xml])[-2:]
        self._xml = xml
        self.last = screens.classify(parse(self._xml), self.package)
        return self.last

    def stable(self, tries: int = 4) -> Screen:
        """Two equal reads in a row. No pause between them: each read already waits for the screen to be quiet for
        500 ms (a playing video: up to 2.5 s), so the second one only confirms."""
        prev = self.screen()
        for _ in range(tries):
            cur = self.screen()
            if _signature(cur.root) == _signature(prev.root):
                return cur
            prev = cur
        raise self.needs_you("screen keeps changing")

    def expect(self, *kinds: Kind) -> Screen:
        s = self.stable()
        if s.kind not in kinds:
            raise self.needs_you(f"expected {'/'.join(k.value for k in kinds)}, got {screens.summary(s)}")
        return s

    def wait_for(self, pred: Callable[[Screen], bool], what: str, timeout: float = 12) -> Screen:
        """Polls until `pred` holds. Promo popups on the way are closed (they show up on launch, at any tab)."""
        end = time.monotonic() + timeout
        closed = 0
        while True:
            s = self.stable()
            if pred(s):
                return s
            if s.kind is Kind.POPUP:
                if closed == MAX_POPUPS:
                    raise self.needs_you(f"{closed} popups in a row")
                self.tap(s, screens.close_button(s.root), "popup close")
                self.log("closed a popup")
                closed += 1
                continue
            if time.monotonic() >= end:
                raise self.needs_you(f"waited for {what}, got {screens.summary(s)}")
            self.pause(1.0)

    def needs_you(self, reason: str) -> NeedsYou:
        path = None
        if self.dump_dir is not None and self._xml:
            self.dump_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
            stamp = time.strftime('%Y%m%d-%H%M%S')
            path = self.dump_dir / f"needs-you-{stamp}.xml"
            path.write_text(self._xml)
            for i, xml in enumerate(reversed(self._earlier), 1):   # the screens before: what it couldn't match
                (self.dump_dir / f"needs-you-{stamp}-before{i}.xml").write_text(xml)
        return NeedsYou(reason, path)

    # --- acting ---

    def tap(self, s: Screen, node: Node, why: str) -> None:
        if not screens.tappable(s, node):
            raise self.needs_you(f"refused tap on {why}: not on the allowlist")
        in_list = node.desc in (*v.LIKE_BUTTONS, v.EDIT_COMMENT)
        if in_list and not screens.profile_view(s.root).safe(node.bounds):
            raise self.needs_you(f"refused tap on {why}: not fully visible or under a floating button")
        self.dev.tap(node.bounds)
        self.pause(0.8)

    def _act(self, s: Screen, node: Node | None, why: str) -> None:
        """A tap that acts (skip, send): only for Dev's explicit commands, on the acting allowlist."""
        if node is None or not screens.actable(s, node):
            raise self.needs_you(f"refused {why}: not on the acting allowlist")
        self.dev.tap(node.bounds)
        self.pause(0.8)

    def ensure_app(self) -> Screen:
        """The app in front on a known screen; launched if another app is."""
        s = self.stable()
        if s.kind is Kind.OTHER_APP:
            self.dev.launch(self.package)
            s = self.wait_for(lambda x: x.kind not in (Kind.OTHER_APP, Kind.UNKNOWN, Kind.POPUP), "the app")
        elif s.kind is Kind.POPUP:
            s = self.wait_for(lambda x: x.kind is not Kind.POPUP, "the app behind the popup")
        if s.kind is Kind.UNKNOWN:
            raise self.needs_you(f"unknown screen: {s.reason}")
        return s

    def go_tab(self, name: str) -> Screen:
        s = self.ensure_app()
        if s.kind is Kind.SHEET:
            raise self.needs_you("comment sheet open: back out first")
        if s.kind is Kind.CHAT:
            s = self.leave_chat()
        if s.selected_tab == name:
            return s
        tab = s.tabs.get(name)
        if tab is None:
            raise self.needs_you(f"no tab {name}")
        self.tap(s, tab.node, f"tab {name}")
        want = {v.TAB_DISCOVER: Kind.FEED, v.TAB_LIKES: Kind.LIKES, v.TAB_MATCHES: Kind.MATCHES}.get(name)
        return self.wait_for(lambda x: x.selected_tab == name and (want is None or x.kind is want), f"tab {name}")

    # --- profile ---

    def _swipe_list(self, view: ProfileView, delta: int) -> None:
        """Scroll the content by `delta` px (> 0: further down the profile). Starts in the list's right margin, beside
        the cards: a video's player takes a swipe that starts on it (the capture took that for the profile's end),
        and the like buttons and the skip button sit on or left of the cards."""
        x = _margin_x(view)
        if delta > 0:
            y1 = view.viewport.bottom - 150
            self.dev.swipe(x, y1, max(view.viewport.top + 50, y1 - delta), SWIPE_MS)
        else:
            y1 = view.viewport.top + 150
            self.dev.swipe(x, y1, min(view.viewport.bottom - 50, y1 - delta), SWIPE_MS)
        self.pause(0.6)

    def _profile(self) -> tuple[Screen, ProfileView]:
        s = self.expect(Kind.FEED)
        return s, screens.profile_view(s.root)

    def _profile_past(self, view: ProfileView, delta: int) -> tuple[Screen, ProfileView, int]:
        """`_profile()` after a swipe by `delta`. An autoplaying video keeps the screen from ever being idle, so
        uiautomator can't read it: swipe on by `delta` until it's off screen. Returns the px swiped blind."""
        extra = 0
        for i in range(MAX_BLIND + 1):
            try:
                s, nxt = self._profile()
                return s, nxt, extra
            except NotIdle:
                if i == MAX_BLIND:
                    break
                self.log("screen unreadable (a playing video?): swiping on")
                self._swipe_list(view, delta)
                extra += delta
        raise _Unreadable

    def _first_profile(self) -> tuple[Screen, ProfileView]:
        """A command's first read, with no dump yet to swipe by: the phone may sit on a playing video. Swipe towards
        the top, beside the cards, until it reads (the screen then isn't a capture's last dump: callers that need
        that offset go back to the top)."""
        for i in range(MAX_BLIND + 1):
            try:
                return self._profile()
            except NotIdle:
                if i == MAX_BLIND:
                    break
                self.log("screen unreadable (a playing video?): swiping up")
                w, h = self.dev.size()
                y1 = int(h * 0.35)   # inside the list, below the collapsed header
                self.dev.swipe(w - BLIND_EDGE, y1, y1 + STEP, SWIPE_MS)
                self.pause(0.6)
        raise self.needs_you("screen stays unreadable (a playing video?) while swiping up")

    def _profile_past_or_ask(self, view: ProfileView, delta: int) -> tuple[Screen, ProfileView, int]:
        try:
            return self._profile_past(view, delta)
        except _Unreadable:
            raise self.needs_you("screen stays unreadable (a playing video?) after swiping past it") from None

    def _track(self, cap: Capture, view: ProfileView, expect: int, blind: bool = False) -> None:
        try:
            cap.add(view, expect=expect, blind=blind)
        except LostTrack as e:
            raise self.needs_you(f"lost track of the profile: {e}") from None

    def _hash_photos(self, cap: Capture, view: ProfileView) -> None:
        if self.hasher is None:
            return
        todo = [(sv, sec) for sv in view.sections if sv.kind == "photo" and not sv.top_cut and not sv.bottom_cut
                and (sec := cap.section_at(sv)) is not None and not sec.hash]
        if todo:
            shot = self.dev.screencap()
            for sv, sec in todo:
                sec.hash = self.hasher(shot, sv.rect)
                if self.cropper is not None:
                    sec.jpeg = self.cropper(shot, sv.rect)
                if self.check_cropper is not None:
                    sec.check_jpeg = self.check_cropper(shot, sv.rect)

    def capture_profile(self) -> Capture:
        """Scrolls through the whole profile from its top, collecting every section. Ends at the bottom."""
        s, view = self._to_top()
        cap = Capture(view)
        self._hash_photos(cap, view)
        rows_seen: list[int] = []
        rows_done: list[int] = []
        self._explore_rows(cap, s, view, rows_seen, rows_done)
        sig = _signature(s.root)
        still = 0
        for _ in range(MAX_SWIPES):
            self._swipe_list(view, STEP)
            try:
                s, nxt, extra = self._profile_past(view, STEP)
            except _Unreadable:
                # swiped well past any video and still moving: the list stopped at the bottom with a video in view
                self._back_to_readable(cap, view)
                break
            if _signature(s.root) == sig:
                still += 1
                if still == 2:
                    break   # bottom: two swipes in a row moved nothing (one can be swallowed)
                continue
            still = 0
            sig = _signature(s.root)
            self._track(cap, nxt, cap.offset + STEP + extra, blind=extra != 0)
            self._hash_photos(cap, nxt)
            self._explore_rows(cap, s, nxt, rows_seen, rows_done)
            view = nxt
        else:
            raise self.needs_you("profile has no end")
        if len(rows_done) < len(rows_seen):
            raise self.needs_you("a detail row was never clear of the floating buttons: its details are missing")
        return cap

    def screenshots(self) -> list[Any]:
        """The profile on screen as Dev sees it, top to end: one half-size image per screen, the next a screen's
        height further (a little overlap). For a remote session, where Dev can't look at the phone. Moves the list, taps nothing."""
        s, view = self._to_top()
        shots = [self.dev.screencap(half=True)]
        sig, still = _signature(s.root), 0
        for _ in range(MAX_SWIPES):
            delta = view.viewport.height - SHOT_OVERLAP
            self._swipe_list(view, delta)
            try:
                s, nxt, _ = self._profile_past(view, delta)
            except _Unreadable:
                break   # stopped at the bottom with a video in view: the last readable screen is in already
            if _signature(s.root) == sig:
                still += 1
                if still == 2:
                    break   # bottom: two swipes in a row moved nothing
                continue
            still, sig, view = 0, _signature(s.root), nxt
            shots.append(self.dev.screencap(half=True))
        else:
            raise self.needs_you("profile has no end")
        return shots

    def _back_to_readable(self, cap: Capture, view: ProfileView) -> None:
        """Ended at the bottom on a video: back up until the screen reads again, so the next command (skip, send)
        finds a readable screen that is the capture's last dump. How far the unseen swipes went is unknown: the
        screen is matched against the last dump by what both show (it is content seen before the video)."""
        for _ in range(MAX_BLIND + 1):
            self._swipe_list(view, -STEP)
            try:
                _, nxt = self._profile()
            except NotIdle:
                continue
            self._track(cap, nxt, cap.offset)
            return
        raise self.needs_you("profile ends on a video and the screen stays unreadable")

    def _explore_rows(self, cap: Capture, s: Screen, view: ProfileView, seen: list[int], done: list[int]) -> None:
        """Swipes each detail row on screen to its end and merges what it reveals (location, children, … hide off
        screen), then flings it back: the vertical stitching needs the screen as it was. Rows by content y."""
        vp = view.viewport
        for row in view.rows:
            if row.top <= vp.top:
                continue   # cut at the top: bounds clipped, its y unknown
            y0 = row.top + cap.offset
            if not any(abs(y0 - y) < ROW_SAME for y in seen):
                seen.append(y0)
            whole = row.bottom < vp.bottom   # bounds are clipped to the list: touching its edge = cut
            if any(abs(y0 - y) < ROW_SAME for y in done) or not whole or not view.safe(row):
                continue   # explored, or cut / under a floating button: a later dump shows it whole
            done.append(y0)
            start = sig = _signature(s.root)
            y = row.center[1]
            for _ in range(MAX_ROW_SWIPES):
                self.dev.swipe_h(y, row.right - ROW_EDGE, row.left + ROW_EDGE, SWIPE_MS)
                s2, v2 = self._profile()
                if _signature(s2.root) == sig:
                    break   # row's end
                sig = _signature(s2.root)
                cap.add_details(v2)
            else:
                raise self.needs_you("detail row has no end")
            for _ in range(3):
                self.dev.swipe_h(y, row.left + ROW_EDGE // 2, row.right - ROW_EDGE // 2, ROW_FLING_MS)
                s2, _ = self._profile()
                if _signature(s2.root) == start:
                    break
            else:
                raise self.needs_you("detail row doesn't go back to its start")

    def _to_top(self) -> tuple[Screen, ProfileView]:
        s, view = self._first_profile()
        for _ in range(MAX_SWIPES):
            if view.at_top:
                return s, view
            self._swipe_list(view, -STEP)
            s, view, _ = self._profile_past_or_ask(view, -STEP)
        if view.at_top:   # the last swipe's screen (seen 2026-11-20: it was the top)
            return s, view
        raise self.needs_you("profile doesn't scroll back to its top")

    def _relocate(self, cap: Capture) -> tuple[Screen, ProfileView]:
        """The screen isn't the capture's last dump (Dev scrolled meanwhile): back to the top, where the capture
        started at offset 0."""
        s, view = self._to_top()
        if view.name != cap.profile.name:
            raise self.needs_you("another profile is on screen")
        cap.restart_at_top(view)
        self.log("relocated: back at the profile's top")
        return s, view

    def scroll_to(self, cap: Capture, ref: str) -> tuple[Screen, SectionView]:
        """Brings a captured section fully on screen with its like button tappable; checks it is the same one."""
        target = cap.profile.find(ref)
        if target is None or not target.complete:
            raise self.needs_you(f"{ref} not captured")
        s, view = self._first_profile()
        if not cap.unmoved(view):
            # Dev scrolled since: matching a far jump can line up wrongly (seen in tests), the top is certain
            s, view = self._relocate(cap)
        for _ in range(MAX_SWIPES):
            sv = next((x for x in view.sections if cap.section_at(x) is target), None)
            if sv and not sv.top_cut and not sv.bottom_cut and sv.like and view.safe(sv.like.bounds):
                self._check_identity(target, sv)
                return s, sv
            want = target.top - (view.viewport.top + 100)    # section's top 100 px below the list's top
            delta = max(-MAX_MOVE, min(MAX_MOVE, want - cap.offset))
            if abs(delta) < 40:
                raise self.needs_you(f"can't bring {ref} to a tappable spot")
            self._swipe_list(view, delta)
            s, view, extra = self._profile_past_or_ask(view, delta)
            self._track(cap, view, cap.offset + delta + extra, blind=extra != 0)
        raise self.needs_you(f"{ref}: too many swipes")

    def _check_identity(self, target: Section, sv: SectionView) -> None:
        if target.kind == "prompt":
            return  # matched by its text already
        if self.hasher is None or not target.hash:
            return
        h = self.hasher(self.dev.screencap(), sv.rect)
        if hash_distance(h, target.hash) > HASH_MAX_DIST:
            raise self.needs_you(f"{target.ref} looks different from the captured photo")

    # --- comment sheet ---

    def open_sheet(self, s: Screen, sv: SectionView) -> Screen:
        self.tap(s, sv.like, f"like button of a {sv.kind}")
        sheet = self.wait_for(lambda x: x.kind is Kind.SHEET, "comment sheet", timeout=6)
        under = screens.sheet_view(sheet.root).section
        if under is None or under.kind != sv.kind or under.text != sv.text:
            raise self.needs_you("comment sheet opened under another section")
        return self._sheet_clear(sheet)

    def _sheet_clear(self, sheet: Screen) -> Screen:
        """The panel opens where the section was, often low behind the floating skip button: scroll until field
        and send button are both fully in the list and clear of it (send stays untouched here, but must be
        reachable for the step that sends)."""
        for _ in range(4):
            view, panel = screens.profile_view(sheet.root), screens.sheet_view(sheet.root)
            if view.safe(panel.field.bounds) and view.safe(panel.send.bounds):
                return sheet
            target = view.viewport.top + int(view.viewport.height * 0.6)   # panel's bottom at 60 % of the list
            delta = max(-MAX_MOVE, min(MAX_MOVE, panel.send.bounds.bottom - target))
            if abs(delta) < 40:
                break
            self._swipe_list(view, delta)
            sheet = self.expect(Kind.SHEET)
        raise self.needs_you("comment panel can't be scrolled clear of the floating buttons")

    def type_comment(self, text: str) -> None:
        """Focus the field, type through our keyboard, read it back: exactly `text` or NeedsYou."""
        s = self.expect(Kind.SHEET)
        self.tap(s, screens.sheet_view(s.root).field, "comment field")
        self.pause(0.7)
        r = self.dev.ime_type(text)
        if not r.ok or r.text != text:
            raise self.needs_you(f"typed text didn't land ({'ok' if r.ok else r.text}, {len(r.text)} chars)")
        back = self.dev.ime_read()
        if not back.ok or back.text != text:
            raise self.needs_you("read-back differs from the typed text")

    def _next_profile(self, name: str, what: str, s: Screen | None = None) -> str:
        """Waits for the feed to show another profile than `name` (the app moves on after a skip or a like)."""
        def moved(x: Screen) -> bool:
            return x.kind is Kind.FEED and screens.profile_view(x.root).name != name
        s = s if s is not None and moved(s) else self.wait_for(moved, what)
        return screens.profile_view(s.root).name

    def skip_profile(self, name: str) -> str:
        """Skips the profile on screen, which must be `name`. Returns the next profile's name."""
        s, view = self._first_profile()
        if view.name != name:
            raise self.needs_you("another profile is on screen: not skipping it")
        self._act(s, screens.skip_button(s.root), "skip")
        return self._next_profile(name, "the next profile after skip")

    def send_like(self, name: str, text: str) -> str:
        """Sends the like from the open comment panel of `name`'s profile, whose field must hold exactly `text`
        (typed by `type_comment`). An upsell sheet gets "send like anyway", never the paid option."""
        s = self.expect(Kind.SHEET)
        view, panel = screens.profile_view(s.root), screens.sheet_view(s.root)
        if view.name != name:
            raise self.needs_you("the panel belongs to another profile: not sending")
        back = self.dev.ime_read()
        if back.ok and back.text == "" and text:
            # field lost focus (a paywall closed over it, Dev tapped elsewhere): reads empty until focused again
            self.tap(s, panel.field, "comment field")
            back = self.dev.ime_read()
        if not back.ok or back.text != text:
            raise self.needs_you("the comment field doesn't hold the comment: not sending")
        if not view.safe(panel.send.bounds):
            raise self.needs_you("send button not clear of the floating buttons")
        self._act(s, panel.send, "send like")
        s = self.wait_for(lambda x: x.kind in (Kind.UPSELL, Kind.OUT_OF_LIKES) or
                          (x.kind is Kind.FEED and screens.profile_view(x.root).name != name), "the like to go out")
        if s.kind is Kind.OUT_OF_LIKES:
            self.tap(s, screens.close_button(s.root), "out-of-likes close")
            # usually the panel comes back; seen 2026-11-19: the app moved on to the next profile instead
            s = self.wait_for(lambda x: x.kind is Kind.SHEET or
                              (x.kind is Kind.FEED and screens.profile_view(x.root).name != name),
                              "the comment panel or the next profile")
            if s.kind is Kind.FEED:
                raise self.needs_you(f"out of free likes for today: not sent, and the app moved on to the next "
                                     f"profile ({screens.profile_view(s.root).name}): this one is gone")
            raise self.needs_you("out of free likes for today: not sent, the comment stays in the panel "
                                 "(send it once the likes are back)")
        if s.kind is Kind.UPSELL:
            if self.dump_dir is not None:   # raw, tmpfs: for a real fixture (the first one was hand-built)
                self.dump_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
                (self.dump_dir / "upsell-last.xml").write_text(self._xml)
            self._act(s, screens.send_anyway_button(s.root), "send like anyway")
        return self._next_profile(name, "the next profile after the like", s)

    def back_out(self, name: str) -> tuple[Screen, bool]:
        """Leave a comment sheet without sending: clear the field, leave the app (BACK hides the keyboard, the next
        leaves; BACK never closes the panel), relaunch. Returns the feed and whether the same profile came back: a
        relaunch can show another one (seen when the profile had been open ~30 min); neither liked nor skipped."""
        self.dev.ime_clear()
        for _ in range(3):
            self.dev.key(KEY_BACK)
            self.pause(0.8)
            if self.dev.foreground() != self.package:
                break
        else:
            raise self.needs_you("BACK doesn't leave the app")
        self.dev.launch(self.package)
        s = self.wait_for(lambda x: x.kind in (Kind.FEED, Kind.SHEET), "the feed after relaunch")
        if s.kind is Kind.SHEET:
            raise self.needs_you("comment panel still open after relaunch")
        return s, screens.profile_view(s.root).name == name

    # --- matches and chat ---

    def open_chat(self, index: int = 0) -> Screen:
        """Opens the n-th visible row of the matches list (marks the chat read in the app)."""
        s = self.expect(Kind.MATCHES)
        rows = screens.match_rows(s.root)
        if index >= len(rows):
            raise self.needs_you(f"no match row {index + 1}")
        self.tap(s, rows[index].node, "match row")
        return self.wait_for(lambda x: x.kind is Kind.CHAT, "chat")

    def leave_chat(self) -> Screen:
        s = self.expect(Kind.CHAT)
        self.tap(s, screens.chat_view(s.root).back, "chat back")
        return self.wait_for(lambda x: x.kind is not Kind.CHAT, "leaving the chat")
