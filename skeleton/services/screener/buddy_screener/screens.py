"""Screen model: which screen the phone shows, and what's on it, from the UI tree alone (no pixels, no Claude).

`classify()` returns a `Screen`; anything it doesn't positively recognise is `UNKNOWN`, and the navigator stops
there instead of tapping blindly. Readers (`profile_view`, `sheet_view`, `match_rows`, `chat_view`) only run on
the screen kind they belong to.

`tappable()` is the allowlist of what the bot may ever tap: tabs, back, match rows, a section's like button, the
comment field. Send buttons, paid elements, skip, photos and chat messages are not on it.
"""

from dataclasses import dataclass, field
from enum import Enum

from . import vocab as v
from .uitree import Node, Rect


class Kind(str, Enum):
    FEED = "feed"            # a profile on the Discover tab
    SHEET = "sheet"          # feed with a section's comment panel open
    LIKES = "likes"          # incoming likes list
    MATCHES = "matches"      # matches list
    CHAT = "chat"            # one conversation
    POPUP = "popup"          # promo layer over the app, closable
    UPSELL = "upsell"        # paid upgrade offered after "send like": "send like anyway" is the way on
    OUT_OF_LIKES = "out_of_likes"   # paywall after "send like" once the day's free likes are used up: not sent
    OTHER_APP = "other_app"  # launcher, system dialog, another app
    UNKNOWN = "unknown"


@dataclass
class Tab:
    name: str
    node: Node               # the clickable/selected wrapper
    selected: bool
    badge: int = 0


@dataclass
class Screen:
    kind: Kind
    root: Node
    tabs: dict[str, Tab] = field(default_factory=dict)
    reason: str = ""         # why UNKNOWN / OTHER_APP

    @property
    def selected_tab(self) -> str | None:
        return next((t.name for t in self.tabs.values() if t.selected), None)


def _tabs(root: Node) -> dict[str, Tab]:
    tabs = {}
    for n in root.iter():
        name = n.desc.split(",", 1)[0]
        if name not in v.TABS or n.cls != "View" or n.parent is None:
            continue
        wrap = n.parent
        if not (wrap.clickable or wrap.selected):
            continue
        badge = v.TAB_BADGE.search(n.desc)
        tabs[name] = Tab(name, wrap, wrap.selected, int(badge.group(1)) if badge else 0)
    return tabs


def _has_desc(root: Node, desc: str) -> bool:
    return root.first(lambda n: n.desc == desc) is not None


def _header(root: Node, text: str) -> bool:
    # list headers sit near the top, above any list content
    return root.first(lambda n: n.cls == "TextView" and n.text == text and n.bounds.top < 500) is not None


def classify(root: Node, package: str) -> Screen:
    if not root.children:
        return Screen(Kind.UNKNOWN, root, reason="empty dump")
    if root.package != package:
        return Screen(Kind.OTHER_APP, root, reason=f"foreground: {root.package or '?'}")
    tabs = _tabs(root)
    s = Screen(Kind.UNKNOWN, root, tabs)
    selected = s.selected_tab

    if send_anyway_button(root) is not None:   # over the sheet: check first
        s.kind = Kind.UPSELL
    elif root.first(lambda n: n.rid == v.CHAT_INPUT) and _has_desc(root, v.BACK):
        s.kind = Kind.CHAT
    elif selected == v.TAB_DISCOVER and _has_desc(root, v.EDIT_COMMENT) and root.first(_is_send_like):
        s.kind = Kind.SHEET
    elif selected == v.TAB_DISCOVER and skip_button(root) and root.first(_is_section):
        s.kind = Kind.FEED
    elif selected == v.TAB_LIKES and _header(root, v.LIKES_HEADER):
        s.kind = Kind.LIKES
    elif selected == v.TAB_MATCHES and _header(root, v.MATCHES_HEADER):
        s.kind = Kind.MATCHES
    elif not tabs and close_button(root) and root.first(lambda n: n.text == v.OUT_OF_LIKES):
        s.kind = Kind.OUT_OF_LIKES   # before POPUP: closing it isn't "on the way", the like didn't go out
    elif not tabs and close_button(root):
        s.kind = Kind.POPUP
    else:
        s.reason = f"no known screen (tab: {selected or 'none'})"
    return s


def _is_like_button(n: Node) -> bool:
    return n.cls == "Button" and n.desc in v.LIKE_BUTTONS


def _is_section(n: Node) -> bool:
    return ((n.cls == "ImageView" and v.PHOTO.fullmatch(n.desc) is not None)
            or (n.cls == "View" and (v.VIDEO.fullmatch(n.desc) or v.VIDEO_PROMPT.fullmatch(n.desc)
                                     or v.VOICE_PROMPT.fullmatch(n.desc) or v.FRIEND_TAKE.fullmatch(n.desc)) is not None)
            or v.PROMPT.fullmatch(n.desc) is not None or _is_poll(n))


def _is_poll(n: Node) -> bool:
    """A poll's question node (its card = the parent). A poll can't be liked here: listed, never commented on."""
    return v.PROMPT.fullmatch(n.desc) is None and v.POLL.fullmatch(n.desc) is not None and n.parent is not None


def _is_option(n: Node) -> bool:
    """A poll option's own node ("Option: <text>"); its clickable parent is the option's row (#82)."""
    return v.POLL_OPTION.fullmatch(n.desc) is not None and n.parent is not None and n.parent.clickable


def option_heart(row: Node) -> Node | None:
    """An option row's heart: the square View without a desc at the row's right end (no content-desc of its own).
    Tapping it opens the comment panel for that option (checked by hand 2026-11-22)."""
    squares = [c for c in row.children if c.cls == "View" and not c.desc and not c.children and not c.bounds.empty
               and abs(c.bounds.width - c.bounds.height) <= 8]
    return max(squares, key=lambda c: c.bounds.right, default=None)


def _is_option_heart(n: Node) -> bool:
    return n.parent is not None and any(_is_option(c) for c in n.parent.children) and option_heart(n.parent) is n


def _is_send_like(n: Node) -> bool:
    return n.cls == "Button" and n.desc in (v.SEND_LIKE, v.SEND_LIKE_WITH)


def send_anyway_button(root: Node) -> Node | None:
    t = root.first(lambda n: n.text == v.SEND_ANYWAY)
    while t is not None and not t.clickable:
        t = t.parent
    return t


def close_button(root: Node) -> Node | None:
    return root.first(lambda n: n.cls == "Button" and n.clickable and n.desc == v.CLOSE)


def skip_button(root: Node) -> Node | None:
    return root.first(lambda n: n.cls == "Button" and n.clickable and v.SKIP.fullmatch(n.desc) is not None)


# --- feed / profile -------------------------------------------------------------------------------------------------

@dataclass
class SectionView:
    """A photo or prompt as one dump shows it. `rect` is clipped to the screen; `top_cut`/`bottom_cut` say which
    edges are the viewport's, not the section's."""
    kind: str                # "photo" | "prompt" | "video" | "voice" | "friend" | "poll" | "option"
    text: str                # prompt: "question\nanswer"; video prompt, poll: the question; option: its text; photo: ""
    rect: Rect
    top_cut: bool
    bottom_cut: bool
    like: Node | None        # its own like button, if on screen
    seen_from: int | None = None   # poll: where its question starts (the card begins higher: a dump can show the
                                   # card's top without the question, so it isn't known there yet)
    poll: str = ""           # option: its poll's question when this dump shows it ("" once it scrolled off)


@dataclass
class ProfileView:
    name: str
    viewport: Rect           # the scroll container
    sections: list[SectionView]
    details: list[tuple[str, str]]   # (label or "", value) as far as visible
    texts: list[SectionView]  # every other text node in the list (details): extra anchors for measuring a scroll
    obstacles: list[Rect]    # clickable things floating over or outside the list (skip, header buttons, tabs)
    at_top: bool = False     # header expanded: the list is scrolled to its start
    rows: list[Rect] = field(default_factory=list)   # sideways-scrolling detail rows: most details hide off screen

    def safe(self, target: Rect) -> bool:
        """A tap target fully inside the list and clear of every floating button."""
        return (not target.empty and self.viewport.contains(target)
                and not any(o.intersects(target) for o in self.obstacles))


def _scroll_container(root: Node) -> Node | None:
    lists = [n for n in root.iter() if n.scrollable and n.cls == "View"]
    return max(lists, key=lambda n: n.bounds.width * n.bounds.height, default=None)


def _inside(n: Node, ancestor: Node) -> bool:
    p = n.parent
    while p is not None:
        if p is ancestor:
            return True
        p = p.parent
    return False


def profile_view(root: Node) -> ProfileView:
    """Sections, details and tap obstacles of the profile on screen (FEED or SHEET)."""
    box = _scroll_container(root)
    skip = skip_button(root)
    if box is None or skip is None:
        raise ValueError("not a profile: no list or no skip button")
    vp = box.bounds
    sections: list[SectionView] = []
    for n in box.find(_is_option):
        row = n.parent
        if row.bounds.empty:
            continue
        # an option is liked on its own (#82): text = the option, its poll's question from the card when visible
        card = row.parent.parent if row.parent is not None else None
        q = card.first(_is_poll) if card is not None else None
        sections.append(SectionView("option", v.POLL_OPTION.fullmatch(n.desc).group(1), row.bounds,
                                    row.bounds.top <= vp.top, row.bounds.bottom >= vp.bottom, option_heart(row),
                                    poll=v.POLL.fullmatch(q.desc).group(1) if q is not None else ""))
    for n in box.find(_is_section):
        if n.bounds.empty:
            continue
        rect, seen_from = n.bounds, None
        if _is_poll(n):
            # question only: the options below may be cut off, the text must match across dumps
            kind, text, rect, seen_from = "poll", v.POLL.fullmatch(n.desc).group(1), n.parent.bounds, n.bounds.top
        elif (m := v.PROMPT.fullmatch(n.desc)) is not None:
            kind, text = "prompt", f"{m.group(1)}\n{m.group(2)}"
        elif v.VIDEO.fullmatch(n.desc) is not None:
            kind, text = "video", ""   # no like attached (LIKE_VIDEO isn't a like button): never liked
        elif (m := v.VIDEO_PROMPT.fullmatch(n.desc)) is not None:
            kind, text = "video", m.group(1)   # the question it answers
        elif (m := v.VOICE_PROMPT.fullmatch(n.desc)) is not None:
            kind, text = "voice", m.group(1)   # recorded answer: the bot can't hear it, never liked (2026-11-20)
        elif (m := v.FRIEND_TAKE.fullmatch(n.desc)) is not None:
            kind, text = "friend", m.group(1)   # a friend's card (carousel, side by side): listed, never liked
        else:
            kind, text = "photo", ""
        sections.append(SectionView(kind, text, rect, rect.top <= vp.top, rect.bottom >= vp.bottom, None, seen_from))

    sections.sort(key=lambda x: x.rect.top)   # options were collected first
    # A section's like button lies over its lower right corner; when the section is cut at the bottom the button can
    # stick out below the list (seen over the tab bar). Attach each to the same-kind section it overlaps most.
    for b in box.find(_is_like_button):
        kind = "prompt" if b.desc == v.LIKE_PROMPT else "photo"
        best = max((s for s in sections if s.kind == kind and s.like is None and s.rect.v_overlap(b.bounds) > 0),
                   key=lambda s: s.rect.v_overlap(b.bounds), default=None)
        if best is not None:
            best.like = b

    # anchors for the shift (a poll's options are sections: they stay after its question scrolled off, 2026-11-21)
    texts = [SectionView("text", n.text, n.bounds, n.bounds.top <= vp.top, n.bounds.bottom >= vp.bottom, None)
             for n in box.iter() if not n.bounds.empty and n.cls == "TextView" and n.text]
    name = v.SKIP.fullmatch(skip.desc).group(1).strip()   # app keeps a trailing space from the profile ("Skip Ana ")
    # At the top of a profile the header is expanded and shows the name as text above the list (it collapses into
    # the skip button's label once scrolled): the list can't move up, so whatever touches its top edge starts there.
    at_top = root.first(lambda n: n.cls == "TextView" and n.text.strip() == name and not _inside(n, box)) is not None
    if at_top:
        for sv in sections + texts:
            sv.top_cut = False
    obstacles = [n.bounds for n in root.iter() if n.clickable and not _inside(n, box) and n is not box]
    # sideways detail rows; a carousel of sections (friends' cards, 2026-11-20) also scrolls sideways but isn't one
    rows = [n.bounds for n in box.iter() if n.scrollable and n.cls == "View" and n is not box and not n.bounds.empty
            and n.first(_is_section) is None]
    return ProfileView(name, vp, sections, _details(box), texts, obstacles, at_top, rows)


def _details(box: Node) -> list[tuple[str, str]]:
    """Icon labels (small Views with a content-desc) paired with the value TextView on their row."""
    labels = [n for n in box.iter() if n.cls == "View" and n.desc and n.bounds.width <= 100 and not n.children]
    promo = _promo_texts(box)
    out = []
    for t in box.iter():
        if t.cls != "TextView" or not t.text or id(t) in promo or v.FRIENDS_HEADER.fullmatch(t.text):
            continue
        cy = t.bounds.center[1]
        row = [l for l in labels if l.bounds.right <= t.bounds.left and l.bounds.top - 20 <= cy <= l.bounds.bottom + 20]
        label = max(row, key=lambda l: l.bounds.right).desc if row else ""   # nearest on the left
        out.append((label, t.text))
    return out


def _promo_texts(box: Node) -> set[int]:
    """TextViews of the app's feature ads ("Add a new layer to your profile…", "Try it"): the smallest card around
    the "Try it" that holds another text."""
    out: set[int] = set()
    for t in box.iter():
        if t.cls != "TextView" or t.text != v.PROMO_ACTION:
            continue
        card = t.parent
        while card is not None and card is not box \
                and not any(x.cls == "TextView" and x.text and x is not t for x in card.iter()):
            card = card.parent
        if card is not None and card is not box:
            out |= {id(x) for x in card.iter() if x.cls == "TextView"}
        else:
            out.add(id(t))
    return out


@dataclass
class SheetView:
    field: Node              # "Edit comment": tap to focus, then type through the keyboard
    send: Node               # never tapped in step 1
    section: SectionView | None   # the section right above the panel (the one being liked)


def sheet_view(root: Node) -> SheetView:
    f = root.first(lambda n: n.desc == v.EDIT_COMMENT)
    send = root.first(_is_send_like)
    if f is None or send is None:
        raise ValueError("no comment sheet")
    above = [s for s in profile_view(root).sections if s.rect.bottom <= f.bounds.top]
    return SheetView(f, send, max(above, key=lambda s: s.rect.bottom, default=None))


# --- lists and chat -------------------------------------------------------------------------------------------------

@dataclass
class MatchRow:
    name: str
    last: str
    node: Node


def match_rows(root: Node) -> list[MatchRow]:
    """Visible rows of the matches list: a clickable row holding the name and the last message."""
    rows = []
    for n in root.iter():
        if not n.clickable:
            continue
        texts = [t.text for t in n.iter() if t.cls == "TextView" and t.text]
        if not texts or v.YOUR_TURN.fullmatch(texts[0]):
            continue
        rows.append(MatchRow(texts[0], texts[1] if len(texts) > 1 else "", n))
    return rows


def likes_empty(root: Node) -> bool:
    return root.first(lambda n: n.cls == "TextView" and n.text.startswith(v.NO_LIKES)) is not None


@dataclass
class Message:
    side: str                # "left" = theirs, "right" = ours (by position: our format is still unseen)
    sender: str
    text: str


@dataclass
class ChatView:
    name: str
    messages: list[Message]  # top to bottom as on screen
    origin: str              # "You liked <name>'s photo." if on screen
    back: Node


def chat_view(root: Node) -> ChatView:
    back = root.first(lambda n: n.desc == v.BACK and n.clickable)
    header = root.first(lambda n: n.rid == v.CHAT_HEADER)
    if back is None or header is None:
        raise ValueError("no chat header")
    name = next((n.text for n in header.iter() if n.cls == "TextView" and n.text not in (v.CHAT_TAB, v.CHAT_PROFILE_TAB)
                 and n.text), "")
    mid = root.children[0].bounds.center[0]
    msgs = []
    for n in root.iter():
        m = v.MESSAGE.fullmatch(n.desc)
        if m and n.cls == "View":
            side = "left" if n.bounds.center[0] < mid else "right"
            msgs.append((n.bounds.top, Message(side, m.group(1), m.group(2))))
    origin = next((n.text for n in root.iter() if v.MATCH_ORIGIN.fullmatch(n.text)), "")
    return ChatView(name, [m for _, m in sorted(msgs, key=lambda p: p[0])], origin, back)


# --- what may be tapped ---------------------------------------------------------------------------------------------

def tappable(screen: Screen, node: Node) -> bool:
    """The allowlist: everything else (send, paid, skip, photos, messages, voice notes) is refused by the navigator."""
    if any(t.node is node and not t.selected for t in screen.tabs.values()):
        return True
    if screen.kind is Kind.FEED:
        return _is_like_button(node) or _is_option_heart(node)
    if screen.kind is Kind.SHEET:
        return node.desc == v.EDIT_COMMENT
    if screen.kind is Kind.MATCHES:
        return any(r.node is node for r in match_rows(screen.root))
    if screen.kind is Kind.CHAT:
        return node.desc == v.BACK and node.clickable
    if screen.kind in (Kind.POPUP, Kind.OUT_OF_LIKES):
        return node is close_button(screen.root)
    return False


def actable(screen: Screen, node: Node) -> bool:
    """The acting allowlist, only for Dev's explicit skip/send commands (never `tappable()`): the skip button, the
    plain send-like button, the upsell's "send like anyway". Never a paid button."""
    if screen.kind is Kind.FEED:
        return node is skip_button(screen.root)
    if screen.kind is Kind.SHEET:
        return _is_send_like(node)
    if screen.kind is Kind.UPSELL:
        return node is send_anyway_button(screen.root)
    return False


def summary(screen: Screen) -> str:
    """One line for logs: kind and counts, never names or texts."""
    s = screen.kind.value
    if screen.reason:
        s += f" ({screen.reason})"
    try:
        if screen.kind in (Kind.FEED, Kind.SHEET):
            p = profile_view(screen.root)
            s += f": {sum(x.kind == 'photo' for x in p.sections)} photos, " \
                 f"{sum(x.kind == 'prompt' for x in p.sections)} prompts on screen"
        elif screen.kind is Kind.MATCHES:
            s += f": {len(match_rows(screen.root))} rows"
        elif screen.kind is Kind.LIKES:
            s += ": empty" if likes_empty(screen.root) else ": not empty"
        elif screen.kind is Kind.CHAT:
            s += f": {len(chat_view(screen.root).messages)} messages"
    except ValueError as e:
        s += f" [{e}]"
    return s
