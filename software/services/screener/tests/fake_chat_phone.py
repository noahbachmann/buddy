"""The fake phone walking the #77 chat fixtures: matches list (scrolls ~200 px, "Their turn" opens on a tap), three
chats, a chat's
Profile tab top to bottom, the More menu.

Two screens are synthetic until step 9 dumps them for real: a chat with text typed (the send button in the voice
note button's place) and the chat after sending (our message as a right-side item)."""

from html import escape

from buddy_screener.device import ImeResult
from buddy_screener.uitree import parse
from fake_phone import FakePhone
from fixture_files import load

PROFILE = [f"chat-profile-{i:02d}" for i in range(1, 19)]
CHATS = {"Alex": "chat-comment", "Robin": "chat-photo-comment", "Sam": "chat-multiline"}
TOPS = {"chat-comment": "chat-comment-top"}       # a swipe down (earlier messages) from a chat's bottom
HINT = '<node class="android.view.View" package="app.example" bounds="[0,1801][1080,1937]">'


def typed(xml: str) -> str:
    return xml.replace("id/microphoneButton", "id/sendButton").replace('content-desc="Record voice note"',
                                                                       'content-desc="Send"')


def with_ours(xml: str, text: str) -> str:
    item = (f'<node class="android.view.View" package="app.example" bounds="[300,1600][1044,1790]">'
            f'<node class="android.view.View" package="app.example" content-desc=" You: {escape(text)}. " '
            f'bounds="[324,1600][1044,1790]" /></node>')
    return xml.replace(HINT, item + HINT, 1)


class FakeChatPhone(FakePhone):
    def __init__(self, screen: str = "matches-turns"):
        super().__init__(screen, walk=PROFILE)
        self.chat = ""                  # the chat open (its bottom fixture)
        self.ours: list[str] = []       # messages we sent in it
        self.unmatched: list[str] = []
        self.field_text: str | None = None
        self.popup_after_swipe = 0      # promo popups that come up over the matches list after a swipe
        self.behind = ""                # the screen under the popup

    def _xml(self) -> str:
        if self.screen.endswith("+typed"):
            return typed(self._xml_of(self.screen[:-6]))
        return self._xml_of(self.screen)

    def _xml_of(self, name: str) -> str:
        xml = load(name)
        for t in self.ours:
            xml = with_ours(xml, t)
        return xml

    def dump(self) -> str:
        return self._xml()

    def _node_at(self, x: int, y: int):
        hits = [n for n in parse(self._xml()).iter()
                if n.bounds.left <= x < n.bounds.right and n.bounds.top <= y < n.bounds.bottom
                and (n.clickable or n.desc or n.text)]
        return hits[-1] if hits else None

    def tap(self, target):
        self.guard()
        x, y = target.center
        n = self._node_at(x, y)
        label = next((c.desc or c.text for c in n.iter() if c.desc or c.text), "") if n else ""
        self.actions.append(("tap", self.screen, label))
        base = self.screen.removesuffix("+typed")
        if label == "Close" and base == "popup":
            self.screen = self.behind
        elif label == "Discover":
            self.screen = "feed-01"
        elif label.startswith("Matches") and base.startswith("feed-"):
            self.screen = "matches-turns"
        elif label.startswith("Their turn") and base in ("matches-turns", "matches-scrolled"):
            self.screen = "matches-expanded"
        elif label in CHATS and base.startswith("matches"):
            self.screen = self.chat = CHATS[label]
        elif label == "Back" and (base.startswith("chat-") and base != "chat-menu"):
            self.screen, self.field = "matches-turns", None
        elif label == "Profile" and base in (self.chat, TOPS.get(self.chat)):
            self.screen = PROFILE[0]
        elif label == "Chat" and base in PROFILE:
            self.screen = TOPS.get(self.chat, self.chat)
        elif label == "More" and base.startswith("chat-"):
            self.screen = "chat-menu"
        elif label == "Unmatch" and base == "chat-menu":
            self.unmatched.append(self.chat)
            self.screen = "matches-turns"
        elif label == "Send a message" and base in (self.chat, TOPS.get(self.chat)):
            self.field, self.ime_shown = self.field or "", True
        elif label == "Send" and self.screen.endswith("+typed"):
            self.ours.append(self.field)
            self.field, self.screen = "", base
        else:
            raise AssertionError(f"unscripted tap on {label!r} in {self.screen}")
        return x, y

    def swipe(self, x, y1, y2, ms):
        self.guard()
        self.actions.append(("swipe", self.screen, y1 - y2, x))
        if self.screen in PROFILE:
            i = PROFILE.index(self.screen) + (1 if y2 < y1 else -1)
            self.screen = PROFILE[max(0, min(len(PROFILE) - 1, i))]
        elif self.screen in ("matches-turns", "matches-scrolled"):
            self.screen = "matches-scrolled" if y2 < y1 else "matches-turns"   # the header collapses: ~200 px
            if self.popup_after_swipe:
                self.popup_after_swipe -= 1
                self.behind, self.screen = self.screen, "popup"
        elif y2 > y1 and self.screen in TOPS:
            self.screen = TOPS[self.screen]
        elif y2 < y1 and self.screen in TOPS.values():
            self.screen = next(k for k, t in TOPS.items() if t == self.screen)
        # matches lists and short chats fit one screen: nothing moves

    def ime_type(self, text: str) -> ImeResult:
        r = super().ime_type(text)
        if self.field:
            self.screen = self.screen.removesuffix("+typed") + "+typed"
        return r

    def ime_clear(self) -> ImeResult:
        r = super().ime_clear()
        self.screen = self.screen.removesuffix("+typed")
        return r

    def sent(self) -> list[tuple]:
        return [a for a in self.actions if a[0] == "tap" and (a[2] in ("Send", "Unmatch") or "Send like" in a[2])]
