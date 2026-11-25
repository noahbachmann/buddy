"""Screen model: which screen the phone shows, and what's on it, from the UI tree alone (no pixels, no Claude).

`classify()` returns a `Screen`; anything it doesn't positively recognise is `UNKNOWN`, and the navigator stops
there instead of tapping blindly. Readers (`profile_view`, `sheet_view`, `match_rows`, `chat_view`) only run on
the screen kind they belong to.

`tappable()` is the allowlist of what the bot may ever tap: tabs, back, match rows, a section's like button, the
comment field, a chat's field, header tabs and More. Send buttons, paid elements, skip, photos, chat messages, the
voice note button and menu items are not on it; `actable()` holds the few acting taps (skip, send, unmatch).
"""

import re
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
    CHAT_PROFILE = "chat_profile"   # a chat's Profile tab: her profile, details listed vertically
    CHAT_MENU = "chat_menu"  # a chat's More menu (popup list): only Unmatch is ever tapped, by the unmatch move
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
        s.kind = Kind.CHAT_PROFILE if chat_tab(root) == v.CHAT_PROFILE_TAB else Kind.CHAT
    elif not tabs and _has_desc(root, v.MENU_HINT) and len(_menu_items(root)) == len(v.MENU_ITEMS):
        s.kind = Kind.CHAT_MENU
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
    return _is_any_option(n) and n.parent.clickable


def _is_any_option(n: Node) -> bool:
    """Also the picked option in a comment panel: its row is no longer clickable (the others are: tapping one
    switches the pick) and keeps its heart, the others lose theirs (dump 2026-11-23)."""
    return v.POLL_OPTION.fullmatch(n.desc) is not None and n.parent is not None


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
    picked: bool = False     # option: the one a comment panel is open for (SHEET only)


@dataclass
class ProfileView:
    name: str
    viewport: Rect           # the scroll container
    sections: list[SectionView]
    details: list[tuple[str, str]]   # (label or "", value) as far as visible
    texts: list[SectionView]  # every other text node in the list (details): extra anchors for measuring a scroll
    obstacles: list[Rect]    # clickable things floating over or outside the list (skip, header buttons, tabs)
    at_top: bool = False     # header expanded: the list is scrolled to its start
    at_bottom: bool = False  # feed: the content ends well above the list's bottom: the profile's end (#89)
    rows: list[Rect] = field(default_factory=list)   # sideways-scrolling detail rows: most details hide off screen
    sideways: list[Rect] = field(default_factory=list)   # everything in the list that scrolls sideways (detail
                                                         # rows, friends' cards): a vertical swipe starting on one
                                                         # can be taken sideways and move nothing (#165)
    promo: list[str] = field(default_factory=list)   # texts of the app's feature ads seen whole in this dump: a
                                                     # dump that cuts off an ad's "Try it" sees its text as a detail

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


CHAT_TOP_GAP = 40   # px of padding above the first content: the chat's Profile tab at its top (scrolled: flush)
END_GAP = 150       # px below the last content: the feed's profile end (306 measured; any other dump is flush, or a
                    # like button sticks out below the list). The chat's Profile tab ends flush: no gap there


def profile_view(root: Node, name: str | None = None) -> ProfileView:
    """Sections, details and tap obstacles of the profile on screen (FEED or SHEET; with `name` from the chat's header:
    a chat's Profile tab, which has no skip button)."""
    box = _scroll_container(root)
    skip = skip_button(root) if name is None else None
    if box is None or (skip is None and name is None):
        raise ValueError("not a profile: no list or no skip button")
    vp = box.bounds
    sections: list[SectionView] = []
    for n in box.find(_is_any_option):
        row = n.parent
        if row.bounds.empty:
            continue
        # an option is liked on its own (#82): text = the option, its poll's question from the card when visible
        card = row.parent.parent if row.parent is not None else None
        q = card.first(_is_poll) if card is not None else None
        sections.append(SectionView("option", v.POLL_OPTION.fullmatch(n.desc).group(1), row.bounds,
                                    row.bounds.top <= vp.top, row.bounds.bottom >= vp.bottom, option_heart(row),
                                    poll=v.POLL.fullmatch(q.desc).group(1) if q is not None else "",
                                    picked=not row.clickable))
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
    if name is None:
        name = v.SKIP.fullmatch(skip.desc).group(1).strip()   # app keeps a trailing space ("Skip Ana ")
        # At the top of a profile the header is expanded and shows the name as text above the list (it collapses
        # into the skip button's label once scrolled): the list can't move up, so whatever touches its top edge
        # starts there.
        at_top = root.first(lambda n: n.cls == "TextView" and n.text.strip() == name
                            and not _inside(n, box)) is not None
    else:
        # the chat's Profile tab: its top keeps padding above the first content, a scrolled list cuts flush (#77)
        tops = [n.bounds.top for n in box.iter() if n is not box and not n.bounds.empty
                and (n.text or n.desc or n.children)]
        at_top = bool(tops) and min(tops) - vp.top >= CHAT_TOP_GAP
    if at_top:
        for sv in sections + texts:
            sv.top_cut = False
    at_bottom = False
    if skip is not None:
        bottoms = [n.bounds.bottom for n in box.iter() if n is not box and not n.bounds.empty
                   and (n.text or n.desc or n.children)]
        at_bottom = bool(bottoms) and vp.bottom - max(bottoms) >= END_GAP
    obstacles = [n.bounds for n in root.iter() if n.clickable and not _inside(n, box) and n is not box]
    # sideways detail rows; a carousel of sections (friends' cards, 2026-11-20) also scrolls sideways but isn't one
    scrollers = [n for n in box.iter() if n.scrollable and n.cls == "View" and n is not box and not n.bounds.empty]
    rows = [n.bounds for n in scrollers if n.first(_is_section) is None]
    promo = _promo_texts(box)
    return ProfileView(name, vp, sections, _details(box, promo), texts, obstacles, at_top, at_bottom, rows,
                       [n.bounds for n in scrollers],
                       [n.text for n in box.iter() if id(n) in promo and n.text and n.text != v.PROMO_ACTION])


def _details(box: Node, promo: set[int]) -> list[tuple[str, str]]:
    """Icon labels (small Views with a content-desc) paired with the value TextView on their row."""
    labels = [n for n in box.iter() if n.cls == "View" and n.desc and n.bounds.width <= 100 and not n.children]
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
    section: SectionView | None   # the one being liked: the section right above the panel, or for a poll the
                                  # picked option (the panel sits under the whole poll card)


def sheet_view(root: Node) -> SheetView:
    f = root.first(lambda n: n.desc == v.EDIT_COMMENT)
    send = root.first(_is_send_like)
    if f is None or send is None:
        raise ValueError("no comment sheet")
    sections = profile_view(root).sections
    above = [s for s in sections if s.rect.bottom <= f.bounds.top and s.kind != "option"]
    section = max(above, key=lambda s: s.rect.bottom, default=None)
    if section is None or section.kind == "poll":   # None: the question scrolled off, the options may not have
        picked = [s for s in sections if s.kind == "option" and s.picked and s.rect.bottom <= f.bounds.top
                  and (section is None or s.poll == section.text)]
        section = picked[0] if len(picked) == 1 else None   # none or several: can't tell which one
    return SheetView(f, send, section)


# --- lists and chat -------------------------------------------------------------------------------------------------

YOUR_TURN, THEIR_TURN = "your_turn", "their_turn"


@dataclass
class MatchRow:
    name: str
    last: str                # her last message as the list shows it (may be cut)
    node: Node
    section: str             # YOUR_TURN | THEIR_TURN | "" (no header above it)
    avatar: Rect | None      # the thumbnail: name + its dhash = the chat's key


@dataclass
class TurnHeader:
    section: str
    count: int
    node: Node               # clickable: a tap expands / collapses the section ("Their turn" starts collapsed)


def turn_headers(root: Node) -> list[TurnHeader]:
    out = []
    for n in root.iter():
        if n.cls != "TextView":
            continue
        for section, rx in ((YOUR_TURN, v.YOUR_TURN), (THEIR_TURN, v.THEIR_TURN)):
            if (m := rx.fullmatch(n.text)) and n.parent is not None:
                out.append(TurnHeader(section, int(m.group(1)), n.parent))
    return sorted(out, key=lambda h: h.node.bounds.top)


def match_rows(root: Node) -> list[MatchRow]:
    """Visible rows of the matches list: a clickable row holding the name and the last message, top to bottom, each
    under the section header above it. The headers themselves are no rows."""
    headers = turn_headers(root)
    rows = []
    for n in root.iter():
        if not n.clickable or n.parent is None or any(h.node is n for h in headers) or _is_tab_wrapper(n):
            continue
        texts = [t.text for t in n.iter() if t.cls == "TextView" and t.text]
        if not texts:
            continue
        above = [h for h in headers if h.node.bounds.bottom <= n.bounds.top]
        thumb = next((c for c in n.children if c.cls == "View" and c.bounds.width == c.bounds.height
                      and c.bounds.left < n.bounds.center[0] and not c.bounds.empty), None)
        rows.append(MatchRow(texts[0], texts[1] if len(texts) > 1 else "", n, above[-1].section if above else "",
                             thumb.bounds if thumb else None))
    return sorted(rows, key=lambda r: r.node.bounds.top)


def _is_tab_wrapper(n: Node) -> bool:
    return any(c.cls == "View" and c.desc.split(",", 1)[0] in v.TABS for c in n.children)


def likes_empty(root: Node) -> bool:
    return root.first(lambda n: n.cls == "TextView" and n.text.startswith(v.NO_LIKES)) is not None


@dataclass
class Message:
    side: str                # "left" = theirs, "right" = ours (by sender; position only without one)
    sender: str
    text: str
    media: bool = False      # an item we can't read as text (photo, voice note, sticker, …): the bot hands over
    top: int = 0             # screen y (for stitching scrolled pages)


@dataclass
class Origin:
    """The chat's head: the section we liked and our like comment (none: "You liked X's photo." instead)."""
    kind: str = ""           # "photo" | "prompt" | "" (not on screen)
    comment: str = ""
    liked_line: str = ""     # "You liked <name>'s photo." when there was no comment
    seen: bool = False       # some part of it is on screen


@dataclass
class ChatView:
    name: str
    messages: list[Message]  # top to bottom as on screen
    origin: Origin
    back: Node
    field: Node | None       # the message field
    send: Node | None        # the send button (only with text typed; else the voice note button sits there)
    tab: str                 # v.CHAT_TAB | v.CHAT_PROFILE_TAB
    tab_nodes: dict[str, Node]   # the header's tabs (clickable wrapper)
    more: Node | None


def chat_tab(root: Node) -> str:
    """Which of the chat header's tabs is shown: the selected one is the one that isn't clickable."""
    for t in (v.CHAT_TAB, v.CHAT_PROFILE_TAB):
        n = root.first(lambda x, t=t: x.cls == "TextView" and x.text == t)
        if n is not None and n.parent is not None and not n.parent.clickable:
            return t
    return ""


def _chat_list(root: Node) -> Node | None:
    """The message list: a reversed column whose direct children are the items (hint, dates, message groups, the
    liked section's card, our comment)."""
    pager = root.first(lambda n: n.rid == "viewPager")
    if pager is None:
        return None
    best = None
    for n in pager.iter():
        if n.cls == "View" and len(n.children) >= 2 and (best is None or len(n.children) > len(best.children)):
            best = n
    return best


def _skipped(item: Node) -> bool:
    """List items that carry nothing: the double-tap hint, date separators, empty leaves (a bubble's background, a
    spacer). An item with children but nothing readable is no spacer: it's a message we can't read (media)."""
    if not item.children and not item.text and not item.desc:
        return True
    texts = [n.text for n in item.iter() if n.cls == "TextView" and n.text]
    if any(t == v.DOUBLE_TAP_HINT for t in texts):
        return True
    return bool(texts) and all(v.DATE_SEPARATOR.fullmatch(t) for t in texts) and not item.first(lambda n: n.desc)


def _liked_line(item: Node) -> str:
    n = item.first(lambda x: x.cls == "TextView" and v.MATCH_ORIGIN.fullmatch(x.text) is not None)
    return n.text if n is not None else ""


def _message_in(item: Node) -> re.Match | None:
    for n in item.iter():
        if n.cls == "View" and (m := v.MESSAGE.fullmatch(n.desc)):
            return m
    return None


def chat_view(root: Node) -> ChatView:
    back = root.first(lambda n: n.desc == v.BACK and n.clickable)
    header = root.first(lambda n: n.rid == v.CHAT_HEADER)
    if back is None or header is None:
        raise ValueError("no chat header")
    tabs = {t: n.parent for t in (v.CHAT_TAB, v.CHAT_PROFILE_TAB)
            if (n := header.first(lambda x, t=t: x.cls == "TextView" and x.text == t)) is not None and n.parent}
    name = next((n.text for n in header.iter() if n.cls == "TextView" and n.text and n.text not in tabs), "")
    mid = root.children[0].bounds.center[0]
    lst = _chat_list(root) if chat_tab(root) == v.CHAT_TAB else None
    items = [n for n in sorted(lst.children if lst else [], key=lambda n: n.bounds.top) if not _skipped(n)]
    msgs: list[Message] = []
    origin = Origin()
    # the anchor: our comment (a bare text item) or "You liked …"; only items above it, before any message, are the
    # liked section's card. Without an anchor on screen, nothing is the head: an unknown item is media.
    first_msg = next((i for i, n in enumerate(items) if _message_in(n)), len(items))
    anchor = next((i for i, n in enumerate(items[:first_msg]) if _liked_line(n) or (n.cls == "TextView" and n.text)),
                  None)
    for i, item in enumerate(items):
        side = "left" if item.bounds.center[0] < mid else "right"
        if (m := _message_in(item)) is not None:
            # by sender, never by position: her long message's row spans the screen, its centre lands on the middle
            # and it read as ours on one page (2026-11-25: the chat turned manual)
            side = "right" if m.group(1).strip() == v.OUR_SENDER else "left" if m.group(1).strip() else side
            msgs.append(Message(side, m.group(1), m.group(2), top=item.bounds.top))
        elif anchor is not None and i < anchor:
            origin.seen = True
            if item.first(lambda n: n.cls == "ImageView" and v.PHOTO.fullmatch(n.desc)) is not None:
                origin.kind = "photo"
            elif item.first(lambda n: n.cls == "TextView" and n.text) is not None:
                origin.kind = origin.kind or "prompt"
        elif i == anchor:
            origin.seen = True
            if (liked := _liked_line(item)):
                origin.liked_line, origin.kind = liked, v.MATCH_ORIGIN.fullmatch(liked).group(2)
            else:
                origin.comment = item.text
        else:
            msgs.append(Message(side, "", "", media=True, top=item.bounds.top))
    field = root.first(lambda n: n.rid == v.CHAT_INPUT)
    box = root.first(lambda n: n.rid == v.CHAT_SEND)
    send = box.first(lambda n: n.clickable and n.rid != v.CHAT_VOICE and n.desc != v.CHAT_VOICE_DESC) if box else None
    more = header.first(lambda n: n.desc == v.MORE and n.clickable)
    return ChatView(name, msgs, origin, back, field, send, chat_tab(root), tabs, more)


def _menu_items(root: Node) -> dict[str, Node]:
    """The More menu's rows by their text: the clickable row holding exactly that text."""
    out = {}
    for n in root.iter():
        if n.cls == "TextView" and n.text in v.MENU_ITEMS and n.parent is not None and n.parent.clickable:
            out[n.text] = n.parent
    return out


def unmatch_item(root: Node) -> Node | None:
    return _menu_items(root).get(v.MENU_UNMATCH)


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
        return any(r.node is node for r in match_rows(screen.root)) or any(
            h.node is node for h in turn_headers(screen.root))
    if screen.kind in (Kind.CHAT, Kind.CHAT_PROFILE):
        c = chat_view(screen.root)
        other = [n for t, n in c.tab_nodes.items() if t != c.tab and n.clickable]
        return node is c.back or node is c.field or node is c.more or any(node is n for n in other)
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
    if screen.kind is Kind.CHAT:
        return node is chat_view(screen.root).send and node is not None
    if screen.kind is Kind.CHAT_MENU:
        return node is unmatch_item(screen.root) and node is not None
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
            c = chat_view(screen.root)
            s += f": {len(c.messages)} messages" + (f", {sum(m.media for m in c.messages)} media" if any(
                m.media for m in c.messages) else "") + (", origin" if c.origin.seen else "")
    except ValueError as e:
        s += f" [{e}]"
    return s
