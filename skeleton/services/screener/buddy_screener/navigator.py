"""Moving around the app on top of `device` and `screens`: tabs, a profile's sections, the comment sheet, a chat.

Rules every move follows:
- act only on a screen that is stable over two dumps (layout can move between a dump and a tap: seen in step 1);
- tap only what `screens.tappable` allows, and on a profile only targets fully inside the list and clear of the
  floating buttons;
- an unknown screen or anything unexpected raises `NeedsYou` (with the dump kept in tmpfs for a fixture), never a
  blind retry.

Nothing here sends anything: no send like, no skip, no chat message (step 3+ adds those, behind their own checks).
"""

import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from . import screens
from . import vocab as v
from .device import KEY_BACK, Device
from .profile import Capture, LostTrack, Section, hash_distance
from .screens import Kind, ProfileView, Screen, SectionView
from .uitree import Node, Rect, parse

STEP = 600            # px per scroll swipe: consecutive dumps share sections (a photo is 960 px)
MAX_MOVE = 700        # px, longest swipe when scrolling to a section: longer ones left no anchor in common
SWIPE_MS = 900        # slow: scrolls by about the swipe length, no fling
MAX_SWIPES = 30
MAX_POPUPS = 3        # closed in a row before asking Dev
ROW_EDGE = 100        # px from a detail row's ends where a sideways swipe starts/ends
ROW_FLING_MS = 150    # fast: flings the row back to its start in one go (measured)
MAX_ROW_SWIPES = 8
ROW_SAME = 100        # px of content y: the same row in another dump
HASH_MAX_DIST = 10    # of 64 bits: same photo
Hasher = Callable[[bytes, Rect], str]   # (screenshot PNG, crop) → dhash
Cropper = Callable[[bytes, Rect], bytes]   # (screenshot PNG, crop) → JPEG for the decision call


class NeedsYou(RuntimeError):
    """Stop and ask Dev: unknown screen, unexpected state, a check failed."""

    def __init__(self, reason: str, dump: Path | None = None):
        super().__init__(reason + (f" (dump: {dump})" if dump else ""))
        self.reason, self.dump = reason, dump


def _signature(root: Node) -> tuple:
    return tuple((n.cls, n.desc, n.text, n.rid, n.bounds, n.selected) for n in root.iter())


@dataclass
class Navigator:
    dev: Device
    package: str
    dump_dir: Path | None = None              # where an unexpected screen's raw dump goes (tmpfs)
    hasher: Hasher | None = None
    cropper: Cropper | None = None            # set: photos keep a JPEG crop (memory only) for the decision
    pause: Callable[[float], object] = time.sleep
    log: Callable[[str], None] = print
    last: Screen | None = field(default=None, init=False)
    _xml: str = field(default="", init=False)

    # --- reading the screen ---

    @property
    def xml(self) -> str:
        """The last raw dump (for `--cmd dump`; personal data: tmpfs only)."""
        return self._xml

    def screen(self) -> Screen:
        self._xml = self.dev.dump()
        self.last = screens.classify(parse(self._xml), self.package)
        return self.last

    def stable(self, tries: int = 4) -> Screen:
        prev = self.screen()
        for _ in range(tries):
            self.pause(0.4)
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
            path = self.dump_dir / f"needs-you-{time.strftime('%Y%m%d-%H%M%S')}.xml"
            path.write_text(self._xml)
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
        """Scroll the content by `delta` px (> 0: further down the profile). Starts in the list's middle column, away
        from the like buttons (right) and the skip button (left)."""
        x = view.viewport.center[0]
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

    def _track(self, cap: Capture, view: ProfileView, expect: int) -> None:
        try:
            cap.add(view, expect=expect)
        except LostTrack as e:
            raise self.needs_you(f"lost track of the profile: {e}") from None

    def _hash_photos(self, cap: Capture, view: ProfileView) -> None:
        if self.hasher is None:
            return
        todo = [(sv, sec) for sv in view.sections if sv.kind == "photo" and not sv.top_cut and not sv.bottom_cut
                and (sec := cap.section_at(sv)) is not None and not sec.hash]
        if todo:
            png = self.dev.screencap()
            for sv, sec in todo:
                sec.hash = self.hasher(png, sv.rect)
                if self.cropper is not None:
                    sec.jpeg = self.cropper(png, sv.rect)

    def capture_profile(self) -> Capture:
        """Scrolls through the whole profile from its top, collecting every section. Ends at the bottom."""
        s, view = self._profile()
        for _ in range(MAX_SWIPES):
            if view.at_top:
                break
            self._swipe_list(view, -STEP)
            s, view = self._profile()
        else:
            raise self.needs_you("profile doesn't scroll back to its top")
        cap = Capture(view)
        self._hash_photos(cap, view)
        rows_seen: list[int] = []
        rows_done: list[int] = []
        self._explore_rows(cap, s, view, rows_seen, rows_done)
        sig = _signature(s.root)
        for _ in range(MAX_SWIPES):
            self._swipe_list(view, STEP)
            s, nxt = self._profile()
            if _signature(s.root) == sig:
                break   # bottom reached: the swipe moved nothing
            sig = _signature(s.root)
            self._track(cap, nxt, cap.offset + STEP)
            self._hash_photos(cap, nxt)
            self._explore_rows(cap, s, nxt, rows_seen, rows_done)
            view = nxt
        else:
            raise self.needs_you("profile has no end")
        if len(rows_done) < len(rows_seen):
            raise self.needs_you("a detail row was never clear of the floating buttons: its details are missing")
        return cap

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

    def scroll_to(self, cap: Capture, ref: str) -> tuple[Screen, SectionView]:
        """Brings a captured section fully on screen with its like button tappable; checks it is the same one."""
        target = cap.profile.find(ref)
        if target is None or not target.complete:
            raise self.needs_you(f"{ref} not captured")
        s, view = self._profile()
        self._track(cap, view, cap.offset)
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
            s, view = self._profile()
            self._track(cap, view, cap.offset + delta)
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
