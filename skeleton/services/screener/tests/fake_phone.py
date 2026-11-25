"""A phone made of fixtures: the screen is one sanitized dump; taps, swipes and keys move between dumps like the
real app did when the dumps were taken (step 1). Records every input action; anything unscripted fails the test."""

from buddy_screener.device import ImeResult, NotIdle
from buddy_screener.uitree import Rect, parse
from fixture_files import FEED, PACKAGE, ROW, load


class FakePhone:
    def __init__(self, screen: str = "feed-01", walk: list[str] | None = None):
        self.screen = screen
        self.walk = walk or FEED         # the dumps a vertical swipe moves through
        self.foreground_pkg = PACKAGE
        self.actions: list[tuple] = []
        self.field: str | None = None   # comment field text while focused, None = no field
        self.ime_shown = False
        self.guard = lambda: None
        self.launch_screen = "feed-01"   # what the app opens on
        self.popup_sticks = False        # Close shows the popup again
        self.upsell = True               # send like → the upsell sheet first
        self.out_of_likes = False        # send like → the out-of-likes paywall, nothing sent
        self.paywall_moves_on = False    # closing that paywall shows the next profile, not the panel
        self.unfocused: str | None = None   # field text kept while the field has no focus (reads empty)
        self.renamed = False             # after a skip or a like: the next profile (same dumps, another name)
        self.moving: set[str] = set()    # screens with a playing video: uiautomator never gets them idle
        self.swallow = 0                 # next N swipes move nothing (a video's player took them)

    # read side

    def dump(self) -> str:
        if self.foreground_pkg != PACKAGE:
            return "<?xml version='1.0' ?><hierarchy rotation=\"0\"><node package=\"launcher.example\" " \
                   "class=\"android.widget.FrameLayout\" bounds=\"[0,0][1080,2280]\" /></hierarchy>"
        if self.screen in self.moving:
            raise NotIdle("uiautomator dump failed: ERROR: could not get idle state.")
        x = load(self.screen)
        return x.replace("Alex", "Kim") if self.renamed else x

    def size(self) -> tuple[int, int]:
        return 1080, 2280

    def screencap(self, half: bool = False):
        raise AssertionError("no screenshots in the fake")

    def foreground(self) -> str:
        return self.foreground_pkg

    # input side

    def _node_at(self, x: int, y: int):
        """The deepest clickable node under the point; its label = its own or its first labelled descendant's."""
        hits = [n for n in parse(load(self.screen)).iter()
                if n.bounds.left <= x < n.bounds.right and n.bounds.top <= y < n.bounds.bottom
                and (n.clickable or n.desc or n.text)]
        return hits[-1] if hits else None

    def tap(self, target: Rect) -> tuple[int, int]:
        self.guard()
        x, y = target.center
        n = self._node_at(x, y)
        label = next((c.desc or c.text for c in n.iter() if c.desc or c.text), "") if n else ""
        self.actions.append(("tap", self.screen, label))
        if label == "Like photo" and self.screen == "feed-01":
            self.screen = "sheet"
        elif label.startswith("Option: ") and self.screen == "feed-poll-all":
            self.screen = "sheet-option"   # an option's heart (no label: its row's); the dump has option 2 picked
        elif label == "Edit comment" and self.screen in ("sheet", "sheet-option-typed"):
            self.field, self.ime_shown, self.unfocused = self.unfocused or "", True, None
        elif label == "Likes You":
            self.screen = "likes"
        elif label.startswith("Matches"):
            self.screen = "matches"
        elif label == "Discover":
            self.screen = "feed-01"
        elif self.screen == "matches" and label == "Sam":   # the row (its name)
            self.screen = "chat"
        elif label == "Back" and self.screen == "chat":
            self.screen = "matches"
        elif label.startswith("Skip ") and self.screen in FEED + ROW:
            self.screen, self.renamed = "feed-01", True
        elif label.startswith("Send like") and self.screen in ("sheet", "sheet-typed") and self.out_of_likes:
            self.unfocused, self.field, self.ime_shown, self.screen = self.field, None, False, "out-of-likes"
        elif label == "Close" and self.screen == "out-of-likes":
            if self.paywall_moves_on:
                self.screen, self.renamed = "feed-01", True
            else:
                self.screen = "sheet"
        elif label.startswith("Send like") and self.screen in ("sheet", "sheet-typed"):
            self.field, self.ime_shown = None, False
            if self.upsell:
                self.screen = "upsell"
            else:
                self.screen, self.renamed = "feed-01", True
        elif label == "Send Like anyway" and self.screen == "upsell":
            self.screen, self.renamed = "feed-01", True
        elif label == "Close" and self.screen == "popup":
            self.screen = "popup" if self.popup_sticks else "feed-01"
        else:
            raise AssertionError(f"unscripted tap on {label!r} in {self.screen}")
        return x, y

    def swipe(self, x: int, y1: int, y2: int, ms: int) -> None:
        self.guard()
        self.actions.append(("swipe", self.screen, y1 - y2, x))
        if self.swallow:
            self.swallow -= 1
        elif self._on_carousel(x, y1):
            pass   # the friends' carousel takes a swipe that starts on it sideways: the list doesn't move (#165)
        elif self.screen in self.walk:
            i = self.walk.index(self.screen) + (1 if y2 < y1 else -1)
            self.screen = self.walk[max(0, min(len(self.walk) - 1, i))]
        elif self.screen == "sheet-low" and y2 < y1:
            self.screen = "sheet"   # the panel moved up, clear of the skip button
        elif self.screen == "sheet-option" and y2 < y1:
            self.screen = "sheet-option-typed"   # same, for an option's panel (that dump was taken typed)

    def _on_carousel(self, x: int, y: int) -> bool:
        """On the friends' carousel: the innermost sideways scroller holding friends' cards (not the list itself)."""
        boxes = [n for n in parse(load(self.screen)).iter() if n.scrollable
                 and n.first(lambda c: c.desc.startswith("Friend's Take")) is not None]
        if not boxes:
            return False
        b = min(boxes, key=lambda n: n.bounds.width * n.bounds.height).bounds
        return len(boxes) > 1 and b.left <= x < b.right and b.top <= y < b.bottom

    def swipe_h(self, y: int, x1: int, x2: int, ms: int) -> None:
        self.guard()
        self.actions.append(("swipe_h", self.screen, x1 - x2))
        if self.screen in ROW:
            i = ROW.index(self.screen)
            self.screen = ROW[min(len(ROW) - 1, i + 1)] if x2 < x1 else ROW[0]   # back = a fling to the start

    def key(self, code: int) -> None:
        self.guard()
        self.actions.append(("key", self.screen, code))
        if code == 224:   # wake up: the fake's screen is always on
            return
        assert code == 4
        if self.ime_shown:
            self.ime_shown = False
        else:
            self.foreground_pkg = "launcher.example"

    def screen_off(self) -> None:
        self.actions.append(("screen_off", self.screen))

    def launch(self, package: str) -> None:
        self.guard()
        self.actions.append(("launch", self.screen, package))
        assert package == PACKAGE
        self.foreground_pkg, self.screen, self.field = PACKAGE, self.launch_screen, None

    def ime_type(self, text: str) -> ImeResult:
        self.guard()
        self.actions.append(("type", self.screen, text))
        if self.field is None:
            return ImeResult(True, "")   # dummy connection: answers ok, nothing lands
        self.field += text
        return ImeResult(True, self.field)

    def ime_clear(self) -> ImeResult:
        self.guard()
        self.actions.append(("clear", self.screen))
        if self.field is not None:
            self.field = ""
        return ImeResult(True, "")

    def ime_read(self) -> ImeResult:
        return ImeResult(True, self.field or "")

    def sent(self) -> list[tuple]:
        """Every tap on something that sends: must stay empty in step 1."""
        acts = self.actions
        return [a for i, a in enumerate(acts) if a[0] == "tap" and ("Send" in a[2] or a[2].startswith("Skip"))
                and not any(b[1] == "out-of-likes" for b in acts[i + 1:i + 2])]   # bounced off the paywall
