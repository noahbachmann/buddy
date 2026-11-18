"""One profile, captured section by section while scrolling: photos and prompts in capture order, each with its
position in content coordinates (y from the top of the list), so the navigator can scroll back to it later.

Scrolling isn't exact (touch slop, a header that collapses on the first swipe), so the offset of each new dump is
measured, not assumed: every section in the new dump votes for the shift that puts one of its known edges onto the
same edge of a same-kind (same-text for prompts) section of the previous dump; the shift with the most votes wins.
A cut section only knows its uncut edge.

Photos all look alike in the tree ("<name>'s photo"), so a photo's identity is its position plus a hash of its
pixels (`dhash`), taken once while it's fully on screen.
"""

import statistics
from collections import Counter
from dataclasses import dataclass, field

from .screens import ProfileView, SectionView
from .uitree import Rect

TOLERANCE = 6   # px: the same edge in two dumps after a measured shift
BLIND_TOLERANCE = 150   # px: an anchor after a blind gap within this of the estimate (swipes vary ~15 px each; photos ~1000 px apart)


class LostTrack(RuntimeError):
    """No shift fits: the list jumped, or this is another profile."""


@dataclass
class Section:
    kind: str                # "photo" | "prompt" | "video" (can't be watched or liked) | "poll" (can't be answered)
    index: int               # 1-based among its kind, capture order: "photo 2", "prompt 1"
    text: str                # prompt "question\nanswer"; photo ""
    top: int | None          # content y of the real edges; None until an uncut edge was seen
    bottom: int | None
    lo: int = 0              # content range seen on screen so far (cut parts excluded)
    hi: int = 0
    hash: str = ""           # photo: dhash of a full view
    jpeg: bytes = field(default=b"", repr=False)   # photo: downscaled crop for the decision call; memory only

    @property
    def ref(self) -> str:
        return f"{self.kind} {self.index}"

    @property
    def complete(self) -> bool:
        return self.top is not None and self.bottom is not None


@dataclass
class Profile:
    name: str
    sections: list[Section] = field(default_factory=list)
    details: list[tuple[str, str]] = field(default_factory=list)

    def find(self, ref: str) -> Section | None:
        return next((s for s in self.sections if s.ref == ref), None)


def _edges(sv: SectionView, offset: int) -> tuple[int | None, int | None]:
    return (None if sv.top_cut else sv.rect.top + offset, None if sv.bottom_cut else sv.rect.bottom + offset)


def _same(a: SectionView, b: SectionView) -> bool:
    return a.kind == b.kind and a.text == b.text


def shift_candidates(prev: ProfileView, prev_offset: int, cur: ProfileView, expect: int | None = None) -> list[int]:
    """Possible offsets of `cur` (content y = screen y + offset), best first: most votes from the sections and texts
    both dumps show, ties (photos look alike) to the one closest to `expect` (the swipe's nominal distance)."""
    votes: Counter[int] = Counter()
    for a in prev.sections + prev.texts:
        at, ab = _edges(a, prev_offset)
        for b in cur.sections + cur.texts:
            if not _same(a, b):
                continue
            if at is not None and not b.top_cut:
                votes[at - b.rect.top] += 1
            if ab is not None and not b.bottom_cut:
                votes[ab - b.rect.bottom] += 1
    near = prev_offset if expect is None else expect
    support = {s: sum(n for t, n in votes.items() if abs(t - s) <= TOLERANCE) for s in votes}
    return sorted(votes, key=lambda s: (-support[s], abs(s - near)))


def verify_shift(prev: ProfileView, prev_offset: int, cur: ProfileView, offset: int) -> bool:
    """Everything `cur` shows (sections and texts) inside `prev`'s visible range in content space has a match in
    `prev` there: a wrong shift puts details where `prev` had a photo, or a photo onto the wrong photo."""
    lo, hi = prev.viewport.top + prev_offset, prev.viewport.bottom + prev_offset
    margin = 30   # px: a node right at the edge may be missing from a dump
    for b in cur.sections + cur.texts:
        bt, bb = b.rect.top + offset, b.rect.bottom + offset
        if bb <= lo + margin or bt >= hi - margin:
            continue  # new content
        if not any(_same(a, b) and min(a.rect.bottom + prev_offset, bb) > max(a.rect.top + prev_offset, bt)
                   for a in prev.sections + prev.texts):
            return False
    return True


class Capture:
    """Feed it every dump of one scroll pass (`add`); it keeps the merged sections."""

    def __init__(self, first: ProfileView, offset: int = 0):
        self.profile = Profile(first.name)
        self.offset = offset
        self.moves: list[tuple[int, int]] = []   # (nominal, measured) px per add(): how swipes really scroll
        self._prev = first
        self._merge(first)

    def unmoved(self, view: ProfileView) -> bool:
        """The view shows exactly what the last tracked dump showed: `offset` still holds."""
        def key(v: ProfileView) -> list:
            return [(x.kind, x.text, x.rect) for x in v.sections + v.texts]
        return view.name == self._prev.name and key(view) == key(self._prev)

    def restart_at_top(self, view: ProfileView) -> None:
        """Tracking resumes from the profile's top (offset 0, where the capture began): after losing track."""
        if not view.at_top:
            raise LostTrack("not at the profile's top")
        self.offset, self._prev = 0, view

    def add(self, view: ProfileView, expect: int | None = None, blind: bool = False) -> int:
        """Measures the new dump's offset, merges its sections, returns the offset. `blind`: swipes since the last
        dump went unseen (a video kept the screen from being read), so the two may share nothing: then the offset
        is `expect` scaled by how far the measured swipes really went; a shared anchor only refines it nearby
        (photos all look alike: a far jump lines up with the wrong one)."""
        if view.name != self.profile.name:
            raise LostTrack("another profile is on screen")
        if blind and expect is not None:
            # median: the first swipe also collapses the header (an outlier)
            ratios = [m / n for n, m in self.moves if n and m / n > 0]
            estimate = self.offset + round((expect - self.offset) * (statistics.median(ratios) if ratios else 1.0))
            offset = next((o for o in shift_candidates(self._prev, self.offset, view, estimate)
                           if abs(o - estimate) <= BLIND_TOLERANCE
                           and verify_shift(self._prev, self.offset, view, o)), estimate)
            self.offset, self._prev = offset, view   # not a measured move: stays out of `moves`
            self._merge(view)
            return offset
        offset = next((o for o in shift_candidates(self._prev, self.offset, view, expect)
                       if verify_shift(self._prev, self.offset, view, o)), None)
        if offset is None:
            raise LostTrack("no shift lines the sections up with the previous dump")
        self.moves.append(((expect if expect is not None else self.offset) - self.offset, offset - self.offset))
        self.offset, self._prev = offset, view
        self._merge(view)
        return offset

    def section_at(self, sv: SectionView, offset: int | None = None) -> Section | None:
        """The captured section this on-screen section is (same kind/text, overlapping in content space)."""
        off = self.offset if offset is None else offset
        top, bottom = sv.rect.top + off, sv.rect.bottom + off
        return next((s for s in self.profile.sections
                     if s.kind == sv.kind and s.text == sv.text and min(s.hi, bottom) > max(s.lo, top)), None)

    def _merge(self, view: ProfileView) -> None:
        for sv in view.sections:
            top, bottom = _edges(sv, self.offset)
            lo, hi = sv.rect.top + self.offset, sv.rect.bottom + self.offset
            s = self.section_at(sv)
            if s is None:
                self.profile.sections.append(Section(sv.kind, 0, sv.text, top, bottom, lo, hi))
                continue
            s.top = s.top if top is None else top
            s.bottom = s.bottom if bottom is None else bottom
            s.lo, s.hi = min(s.lo, lo), max(s.hi, hi)
        # capture order = content order; indexes follow it
        self.profile.sections.sort(key=lambda s: s.lo)
        count: Counter[str] = Counter()
        for s in self.profile.sections:
            count[s.kind] += 1
            s.index = count[s.kind]
        self.add_details(view)

    def add_details(self, view: ProfileView) -> None:
        """Merges the view's details; a value seen without its label (icon scrolled off) gets it later."""
        details = self.profile.details
        for label, value in view.details:
            same = next((i for i, (l, v) in enumerate(details) if v == value and (l == label or not l or not label)), None)
            if same is None:
                details.append((label, value))
            elif label and not details[same][0]:
                details[same] = (label, value)   # a later dump showed its label


def dhash(gray_rows: list[list[int]]) -> str:
    """Difference hash of a 9×8 grayscale thumbnail (rows of 9 values): 64 bits as hex."""
    bits = 0
    for row in gray_rows:
        for x in range(8):
            bits = (bits << 1) | (row[x] > row[x + 1])
    return f"{bits:016x}"


def hash_distance(a: str, b: str) -> int:
    return bin(int(a, 16) ^ int(b, 16)).count("1")
