"""A `uiautomator dump` as a tree of nodes: what the screen model reads.

The app's Compose screens carry their labels in `content-desc` (no resource ids); the chat is classic views with
resource ids. Both end up in `Node`: `desc`, `text`, `rid` (the id without its package prefix).
"""

import re
import xml.etree.ElementTree as ET
from collections.abc import Callable, Iterator
from dataclasses import dataclass, field

_BOUNDS = re.compile(r"\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]")


@dataclass(frozen=True)
class Rect:
    left: int
    top: int
    right: int
    bottom: int

    @classmethod
    def parse(cls, text: str) -> "Rect":
        m = _BOUNDS.fullmatch(text)
        if not m:
            raise ValueError(f"bad bounds {text!r}")
        return cls(*map(int, m.groups()))

    @property
    def width(self) -> int:
        return self.right - self.left

    @property
    def height(self) -> int:
        return self.bottom - self.top

    @property
    def center(self) -> tuple[int, int]:
        return (self.left + self.right) // 2, (self.top + self.bottom) // 2

    @property
    def empty(self) -> bool:
        return self.width <= 0 or self.height <= 0

    def contains(self, other: "Rect") -> bool:
        return (self.left <= other.left and self.top <= other.top
                and other.right <= self.right and other.bottom <= self.bottom)

    def intersects(self, other: "Rect") -> bool:
        return (self.left < other.right and other.left < self.right
                and self.top < other.bottom and other.top < self.bottom)

    def v_overlap(self, other: "Rect") -> int:
        return max(0, min(self.bottom, other.bottom) - max(self.top, other.top))

    def shifted(self, dy: int) -> "Rect":
        return Rect(self.left, self.top + dy, self.right, self.bottom + dy)

    def __str__(self) -> str:
        return f"[{self.left},{self.top}][{self.right},{self.bottom}]"


@dataclass(eq=False)
class Node:
    cls: str          # short class name: "Button", "TextView", "View"
    text: str
    desc: str
    rid: str          # resource id without "<package>:id/"
    package: str
    bounds: Rect
    clickable: bool = False
    enabled: bool = True
    focused: bool = False
    scrollable: bool = False
    selected: bool = False
    children: list["Node"] = field(default_factory=list)
    parent: "Node | None" = field(default=None, repr=False)

    def iter(self) -> Iterator["Node"]:
        """This node and all below it, document order."""
        yield self
        for c in self.children:
            yield from c.iter()

    def find(self, pred: Callable[["Node"], bool]) -> list["Node"]:
        return [n for n in self.iter() if pred(n)]

    def first(self, pred: Callable[["Node"], bool]) -> "Node | None":
        return next((n for n in self.iter() if pred(n)), None)

    def label(self) -> str:
        return self.desc or self.text

    def __str__(self) -> str:
        return f"{self.cls} {self.bounds} rid={self.rid!r} t={self.text!r} d={self.desc!r}"


def _bool(a: dict, key: str) -> bool:
    return a.get(key) == "true"


def _node(el: ET.Element, parent: Node | None) -> Node:
    a = el.attrib
    rid = a.get("resource-id", "")
    n = Node(
        cls=a.get("class", "").rsplit(".", 1)[-1],
        text=a.get("text", ""),
        desc=a.get("content-desc", ""),
        rid=rid.split(":id/", 1)[-1],
        package=a.get("package", ""),
        bounds=Rect.parse(a.get("bounds", "[0,0][0,0]")),
        clickable=_bool(a, "clickable"),
        enabled=a.get("enabled", "true") == "true",
        focused=_bool(a, "focused"),
        scrollable=_bool(a, "scrollable"),
        selected=_bool(a, "selected"),
        parent=parent,
    )
    n.children = [_node(c, n) for c in el if c.tag == "node"]
    return n


def parse(xml: str) -> Node:
    """The dump's root: a synthetic node holding the window's top nodes. Raises ValueError on a broken dump."""
    # `exec-out uiautomator dump /dev/tty` appends "UI hierchary dumped to: /dev/tty" after the XML
    end = xml.rfind("</hierarchy>")
    if end < 0:
        raise ValueError("no <hierarchy> in the dump")
    try:
        root = ET.fromstring(xml[: end + len("</hierarchy>")])
    except ET.ParseError as e:
        raise ValueError(f"unparsable dump: {e}") from None
    top = Node("hierarchy", "", "", "", "", Rect(0, 0, 0, 0))
    top.children = [_node(c, top) for c in root if c.tag == "node"]
    top.package = top.children[0].package if top.children else ""
    return top
