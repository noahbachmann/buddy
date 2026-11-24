"""Real `uiautomator` dump → test fixture with nothing personal or app-identifying left.

Kept: the tree's shape, classes, bounds, flags, resource ids, and the UI strings in `vocab` (exact or by pattern).
Replaced: names (consistently: "Skip Noëlle", "Noëlle's photo" and the header "Noëlle" all become the same
placeholder), prompts, messages, details, every other text → numbered placeholders; the app's package →
`app.example`. Default-valued attributes are dropped (smaller fixtures; `uitree` fills the defaults).

The result is checked against a forbidden word list (host-only file + the package's distinctive parts): a hit
raises instead of writing a fixture that would leak them into the public repo.
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from . import vocab as v

PACKAGE = "app.example"
NAMES = ["Alex", "Sam", "Robin", "Kim", "Noa", "Jo", "Lou", "Mika", "Ari", "Tal"]
_DEFAULTS = {"checkable": "false", "checked": "false", "clickable": "false", "enabled": "true", "focusable": "false",
             "focused": "false", "scrollable": "false", "long-clickable": "false", "password": "false",
             "selected": "false", "text": "", "resource-id": "", "content-desc": ""}
_GENERIC_PARTS = {"com", "app", "android", "google", "mobile", "apps", "www"}


class Leak(ValueError):
    pass


class _Mapper:
    def __init__(self):
        self.names: dict[str, str] = {}
        self.other: dict[str, str] = {}
        self.prompts: dict[str, int] = {}

    def name(self, real: str) -> str:
        if real not in self.names:
            self.names[real] = NAMES[len(self.names) % len(NAMES)] + ("" if len(self.names) < len(NAMES) else
                                                                      str(len(self.names)))
        return self.names[real]

    def placeholder(self, real: str, kind: str) -> str:
        key = f"{kind}\0{real}"
        if key not in self.other:
            self.other[key] = f"{kind} {sum(k.startswith(kind + chr(0)) for k in self.other) + 1}"
        return self.other[key]

    def collect(self, s: str) -> None:
        """First pass: names from the patterns that carry them."""
        for rx in (v.SKIP, v.PHOTO, v.VIDEO, v.MATCH_ORIGIN, v.CHAT_NAME_VERIFIED):
            if (m := rx.fullmatch(s)) is not None:
                self.name(m.group(1))
        if (m := v.MESSAGE.fullmatch(s)) is not None:
            self.name(m.group(1))

    def clean(self, s: str) -> str:
        if not s or s in v.KEEP:
            return s
        if (m := v.TAB_BADGE.search(s)) and s.split(",", 1)[0] in v.TABS:
            return s
        if v.YOUR_TURN.fullmatch(s) or v.THEIR_TURN.fullmatch(s):
            return s
        if (m := v.CHAT_NAME_VERIFIED.fullmatch(s)) is not None:
            return f"{self.name(m.group(1))}, Verified"
        if (m := v.PHOTO_PROMPT.fullmatch(s)) is not None:
            return f"Photo prompt: {self.placeholder(m.group(1), 'Question')}"
        if s.startswith(v.NO_LIKES):
            return v.NO_LIKES
        if (m := v.SKIP.fullmatch(s)) is not None:
            return f"Skip {self.name(m.group(1))}"
        if (m := v.MATCH_ORIGIN.fullmatch(s)) is not None:
            return f"You liked {self.name(m.group(1))}'s {m.group(2)}."
        if (m := v.PHOTO.fullmatch(s)) is not None:
            return f"{self.name(m.group(1))}'s photo"
        if (m := v.VIDEO.fullmatch(s)) is not None:
            return f"{self.name(m.group(1))}'s video"
        if (m := v.VIDEO_PROMPT.fullmatch(s)) is not None:
            return f"Video prompt: {self.placeholder(m.group(1), 'Question')}"
        if (m := v.VOICE_PROMPT.fullmatch(s)) is not None:
            return f"Voice Prompt: {self.placeholder(m.group(1), 'Question')}"
        if (m := v.FRIEND_TAKE.fullmatch(s)) is not None:
            return f"Friend's Take. {self.placeholder(m.group(1), 'Take')}"
        if (m := v.LIKE_FRIEND_TAKE.fullmatch(s)) is not None:
            return f"Like {self.name(m.group(1))}'s Friend's Take"
        if (m := v.FRIENDS_HEADER.fullmatch(s)) is not None:
            return f"From people close to {self.name(m.group(1))}"
        if v.ELAPSED.fullmatch(s):
            return s
        if (m := v.PROMPT.fullmatch(s)) is not None:
            return f"Prompt: {self.placeholder(m.group(1), 'Question')}. Answer: {self.placeholder(m.group(2), 'Answer')}"
        if (m := v.POLL.fullmatch(s)) is not None:
            return f"Prompt: {self.placeholder(m.group(1), 'Question')}"
        if (m := v.POLL_OPTION.fullmatch(s)) is not None:
            return f"Option: {self.placeholder(m.group(1), 'Option')}"
        if (m := v.MESSAGE.fullmatch(s)) is not None:
            return f" {self.name(m.group(1))}: {self.placeholder(m.group(2), 'Message')}. "
        if v.DATE_SEPARATOR.fullmatch(s):
            return "Today 12:00"
        if s in self.names:
            return self.names[s]
        return self.placeholder(s, "Text")


def forbidden_words(package: str, extra: list[str]) -> list[str]:
    parts = [p for p in re.split(r"[.\-_]", package) if len(p) >= 4 and p.lower() not in _GENERIC_PARTS]
    return [w.strip() for w in [package, *parts, *extra] if w.strip()]


def read_forbidden(path: Path) -> list[str]:
    """One word per line, '#' comments. Missing file = none."""
    if not path.exists():
        return []
    return [line.split("#", 1)[0].strip() for line in path.read_text().splitlines() if line.split("#", 1)[0].strip()]


def sanitize(xml: str, package: str, forbidden: list[str] = ()) -> str:
    return sanitize_many([xml], package, forbidden)[0]


def sanitize_many(xmls: list[str], package: str, forbidden: list[str] = ()) -> list[str]:
    """One mapping across all dumps: a walk's dumps keep "Prompt 2" the same prompt in every file."""
    roots = []
    for xml in xmls:
        end = xml.rfind("</hierarchy>")
        roots.append(ET.fromstring(xml[: end + len("</hierarchy>")]))
    nodes = [n for r in roots for n in r.iter("node")]
    m = _Mapper()
    for n in nodes:
        for key in ("text", "content-desc"):
            m.collect(n.attrib.get(key, ""))
    for n in nodes:
        a = n.attrib
        for key in ("text", "content-desc"):
            if key in a:
                a[key] = m.clean(a[key])
        if a.get("package") == package:
            a["package"] = PACKAGE
        if a.get("resource-id", "").startswith(package + ":"):
            a["resource-id"] = PACKAGE + a["resource-id"][len(package):]
        for k, d in _DEFAULTS.items():
            if a.get(k) == d:
                del a[k]
        a.pop("index", None)
    words = forbidden_words(package, list(forbidden))
    outs = []
    for r in roots:
        ET.indent(r, " ")
        out = "<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>\n" + ET.tostring(r, encoding="unicode") + "\n"
        low = out.lower()
        hits = {w for w in words if w.lower() in low}
        if hits:
            raise Leak(f"sanitized dump still contains {len(hits)} forbidden word(s)")  # never print them: logs
        outs.append(out)
    return outs


def main(argv: list[str]) -> int:
    """python -m buddy_screener.sanitize <package> <forbidden file> <out dir> <in.xml>...: same file names in out dir"""
    package, forbidden, out_dir, *files = argv
    paths = [Path(f) for f in files]
    outs = sanitize_many([p.read_text() for p in paths], package, read_forbidden(Path(forbidden)))
    for p, out in zip(paths, outs):
        (Path(out_dir) / p.name).write_text(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
