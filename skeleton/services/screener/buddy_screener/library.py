"""Dev's host-only input: `preferences.md` (the system prompt's core) and `templates.md` (fixed texts).

Both live in `~/buddy/config/screener/` on the host, never in Git. Loaded fresh for every decision, so a template
added during a shadow session is available to the next profile.

`templates.md` sections are `## <name>`. The *Like comments* section holds the library the bot picks from when it
likes a section; every other line in it is free text (format notes) and ignored:

    ## Like comments
    - <id> | <de|en> | <fit hint: what it suits> | <text, pasted verbatim>

The model only ever returns an id; code pastes the text unchanged, so the checks here are the app's limits.
"""

import re
from dataclasses import dataclass
from pathlib import Path

COMMENT_MAX = 255           # the app's comment limit (step 0): over it, send greys out
LIKE_SECTION = "like comments"
_ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,39}")
_LINE = re.compile(r"-\s+(\S+)\s*\|\s*(\S+)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*")


class LibraryError(ValueError):
    """A broken preferences/templates file: refuse to decide on it."""


@dataclass(frozen=True)
class LikeComment:
    id: str
    lang: str               # "de" | "en"
    hint: str               # what it fits: the model chooses by this
    text: str


def sections(md: str) -> dict[str, str]:
    """`## name` → body, names lower-cased."""
    out: dict[str, list[str]] = {}
    cur: list[str] | None = None
    for line in md.splitlines():
        if line.startswith("## "):
            cur = out.setdefault(line[3:].strip().lower(), [])
        elif cur is not None:
            cur.append(line)
    return {k: "\n".join(v).strip() for k, v in out.items()}


def parse_like_comments(md: str) -> list[LikeComment]:
    body = sections(md).get(LIKE_SECTION)
    if body is None:
        return []
    errors, out, seen = [], [], set()
    for n, line in enumerate(body.splitlines(), 1):
        if not line.startswith("- "):
            continue
        m = _LINE.fullmatch(line.rstrip())
        if m is None:
            errors.append(f"line {n}: not '- id | lang | hint | text'")
            continue
        cid, lang, hint, text = m.groups()
        if not _ID.fullmatch(cid):
            errors.append(f"line {n}: id {cid!r}: lower-case letters, digits, - and _ only")
        elif cid in seen or cid == "none":
            errors.append(f"line {n}: id {cid!r} used twice (or reserved)")
        if lang not in ("de", "en"):
            errors.append(f"line {n}: language {lang!r}: de or en")
        if len(text) > COMMENT_MAX:
            errors.append(f"line {n}: {len(text)} chars, the app takes {COMMENT_MAX}")
        if "{" in text or "}" in text:
            errors.append(f"line {n}: no placeholders in a like comment (pasted verbatim)")
        seen.add(cid)
        out.append(LikeComment(cid, lang, hint, text))
    if errors:
        raise LibraryError("templates.md, Like comments: " + "; ".join(errors))
    return out


@dataclass(frozen=True)
class Library:
    preferences: str
    like_comments: tuple[LikeComment, ...]

    def comment(self, cid: str) -> LikeComment | None:
        return next((c for c in self.like_comments if c.id == cid), None)

    @classmethod
    def load(cls, config_dir: Path) -> "Library":
        try:
            prefs = (config_dir / "preferences.md").read_text()
            templates = (config_dir / "templates.md").read_text()
        except OSError as e:
            raise LibraryError(f"can't read {e.filename}: {e.strerror}") from None
        if not prefs.strip():
            raise LibraryError("preferences.md is empty")
        return cls(prefs.strip(), tuple(parse_like_comments(templates)))
