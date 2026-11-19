"""Dev's host-only input: `preferences.md` (the system prompt's core), `templates.md` (fixed texts) and `chat.md`
(the chat's rules, #77).

All live in `~/buddy/config/screener/` on the host, never in Git. Loaded fresh for every decision, so a template
added during a shadow session is available to the next profile.

`templates.md` sections are `## <name>`. The *Like comments* section holds the library the bot picks from when it
likes a section; every other line in it is free text (format notes) and ignored:

    ## Like comments
    - <id> | <de|en> | <fit hint: what it suits> | <text, pasted verbatim>

The model only ever returns an id; code pastes the text unchanged, so the checks here are the app's limits.

The chat templates (`## disclosure`, `## contact_offer`, …) are `EN:` / `DE:` blocks, hard-wrapped (lines joined
with a space); lines before the first block are notes. Code fills their placeholders; `{number}` / `{instagram}`
come from the files of the same name, which only `Contacts` reads and no prompt ever contains.
"""

import re
from dataclasses import dataclass, field
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


# --- the chat (#77) --------------------------------------------------------------------------------------------------

# template → placeholders it must contain beyond the always-allowed {name}
CHAT_TEMPLATES = {"disclosure": set(), "contact_offer": {"fit_line"}, "contact_number": {"number"},
                  "contact_instagram": {"instagram"}, "closing": set(), "opt_out": set()}
CHAT_LANGS = ("en", "de")
# preferences.md sections the chat calls see (heading prefixes); not "Physical": a chat shows no photos
CHAT_PREFS = ("about dev", "looking for", "good fit", "soft negatives", "soft positives", "dealbreakers")
_BLOCK = re.compile(r"(EN|DE):\s*(.*)")
_PLACEHOLDER = re.compile(r"\{(\w+)\}")
_QUESTION = re.compile(r"(\d+)\.\s+(.*)")
_URL = re.compile(r"https?://[^\s\"')<>]+")
REQUIRED = "**(required)**"


def parse_chat_templates(md: str) -> dict[str, dict[str, str]]:
    """The chat templates: name → {"en": text, "de": text}. Any missing or broken one refuses the whole file."""
    secs, errors, out = sections(md), [], {}
    for name, needs in CHAT_TEMPLATES.items():
        body = secs.get(name)
        if body is None:
            errors.append(f"## {name} missing")
            continue
        blocks: dict[str, list[str]] = {}
        cur: list[str] | None = None
        for line in body.splitlines():
            if m := _BLOCK.match(line):
                cur = blocks.setdefault(m.group(1).lower(), [])
                line = m.group(2)
            if cur is not None:
                cur.append(line.strip())
        texts = {}
        for lang in CHAT_LANGS:
            text = " ".join(x for x in blocks.get(lang, []) if x)
            if not text:
                errors.append(f"## {name}: no {lang.upper()}: block")
                continue
            used = set(_PLACEHOLDER.findall(text))
            if extra := used - needs - {"name"}:
                errors.append(f"## {name} {lang.upper()}: placeholder {sorted(extra)} not allowed here")
            if missing := needs - used:
                errors.append(f"## {name} {lang.upper()}: needs {sorted(missing)}")
            texts[lang] = text
        out[name] = texts
    if errors:
        raise LibraryError("templates.md: " + "; ".join(errors))
    return out


def fill(template: str, **values: str) -> str:
    """Placeholders by code; a value can't inject another placeholder (one pass)."""
    return _PLACEHOLDER.sub(lambda m: values[m.group(1)], template)


@dataclass(frozen=True)
class Question:
    n: int
    text: str
    required: bool


def parse_questions(md: str) -> tuple[Question, ...]:
    body = sections(md).get("screening questions", "")
    qs: list[list] = []
    for line in body.splitlines():
        if m := _QUESTION.match(line):
            qs.append([int(m.group(1)), m.group(2).strip()])
        elif qs and line.startswith((" ", "\t")) and line.strip():
            qs[-1][1] += " " + line.strip()
    out = tuple(Question(n, t.replace(REQUIRED, "").strip(), REQUIRED in t) for n, t in qs)
    if not out:
        raise LibraryError("chat.md: no numbered screening questions")
    if not any(q.required for q in out):
        raise LibraryError(f"chat.md: no question marked {REQUIRED}")
    if len({q.n for q in out}) != len(out):
        raise LibraryError("chat.md: a question number used twice")
    return out


def chat_preferences(prefs_md: str) -> str:
    """The preferences.md sections a chat needs, headings kept, in file order."""
    keep, cur = [], False
    for line in prefs_md.splitlines():
        if line.startswith("## "):
            cur = line[3:].strip().lower().startswith(CHAT_PREFS)
        if cur:
            keep.append(line)
    text = "\n".join(keep).strip()
    if "## about" not in text.lower():
        raise LibraryError("preferences.md: no ## About Dev section (the only facts a chat may share)")
    return text


@dataclass(frozen=True)
class ChatRules:
    rules: str                               # chat.md verbatim
    preferences: str                         # the chat's preferences.md sections
    questions: tuple[Question, ...]
    templates: dict[str, dict[str, str]] = field(hash=False)
    urls: frozenset[str] = frozenset()       # links chat.md itself contains: allowed in a generated reply

    @property
    def required(self) -> tuple[int, ...]:
        return tuple(q.n for q in self.questions if q.required)

    @classmethod
    def load(cls, config_dir: Path) -> "ChatRules":
        try:
            rules = (config_dir / "chat.md").read_text()
            prefs = (config_dir / "preferences.md").read_text()
            templates = (config_dir / "templates.md").read_text()
        except OSError as e:
            raise LibraryError(f"can't read {e.filename}: {e.strerror}") from None
        return cls(rules.strip(), chat_preferences(prefs), parse_questions(rules), parse_chat_templates(templates),
                   frozenset(_URL.findall(rules)))


@dataclass(frozen=True)
class Contacts:
    """Dev's contact values. Only code fills them into a template; never in a prompt, a log or a repr."""
    number: str = field(repr=False)
    instagram: str = field(repr=False)

    @classmethod
    def load(cls, config_dir: Path) -> "Contacts":
        try:
            number, instagram = ((config_dir / f).read_text().strip() for f in ("number", "instagram"))
        except OSError as e:
            raise LibraryError(f"can't read {e.filename}: {e.strerror}") from None
        if not number or not instagram:
            raise LibraryError("number / instagram file empty")
        return cls(number, instagram)

    def values(self) -> tuple[str, ...]:
        return self.number, self.instagram
