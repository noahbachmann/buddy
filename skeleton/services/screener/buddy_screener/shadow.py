"""Step 2 shadow mode: the bot judges the profile on screen, Dev acts by hand and labels (#66 step 2).

A session is a sequence of offline commands (run from Claude's session or a terminal on the host):

1. `shadow`: capture the profile on screen, decide, store the bot's decision, print it (texts: host terminal only).
2. Dev decides on the phone. `label <id> like|skip [section] [comment]`: Dev's choice; a like without section and
   comment takes the bot's. A comment Dev writes themselves goes into `templates.md` first, then gets labelled by id.
3. `type <id>` (a like with a comment): scroll to the labelled section, open its panel, type the comment through our
   keyboard and read it back. **Dev taps send**: nothing here sends, skips or likes.
4. `report`: the done-when numbers.

The capture between `shadow` and `type` waits in tmpfs (pickled, without photo crops): `type` must find the same
profile on screen, else it stops.
"""

import pickle
import re
import statistics
import time
from dataclasses import dataclass
from pathlib import Path

from . import vocab
from .chat import refusal
from .decide import COMMENT_MAX, TASK_BROWSE, TASK_TEXT, Browse, Decider, Usage
from .library import Library
from .navigator import Navigator
from .profile import Capture
from .store import ShadowRow, Store, profile_key

# done-when of step 2 (#66)
GATE_PROFILES = 50
GATE_AGREEMENT = 0.85
GATE_TEMPLATE = 0.80


class LabelError(ValueError):
    pass


def _capture_file(tmp_dir: Path, row_id: int) -> Path:
    return tmp_dir / f"shadow-{row_id}.pkl"


@dataclass
class ShadowResult:
    row_id: int
    capture: Capture
    browse: Browse
    usage: Usage
    capture_s: float


def run_shadow(nav: Navigator, decider: Decider, lib: Library, store: Store, tmp_dir: Path) -> ShadowResult:
    nav.go_tab(vocab.TAB_DISCOVER)
    t = time.monotonic()
    cap = nav.capture_profile()
    capture_s = time.monotonic() - t
    p = cap.profile
    browse, usage = decider.decide(p, lib)
    first_prompt = next((s.text for s in p.sections if s.kind == "prompt"), "")
    bot = browse.to_dict() | {"chat_lang": browse.chat_lang}
    row_id = store.add_shadow(profile_key(p.name, p.details, first_prompt), capture_s, bot, usage)

    for s in p.sections:
        s.jpeg = b""             # crops never leave memory
    tmp_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    for old in tmp_dir.glob("shadow-*.pkl"):
        old.unlink()
    path = _capture_file(tmp_dir, row_id)
    path.write_bytes(pickle.dumps(cap))
    path.chmod(0o600)
    return ShadowResult(row_id, cap, browse, usage, capture_s)


def describe(r: ShadowResult, lib: Library) -> list[str]:
    """For the host terminal: what the bot decided and on what. Contains profile text."""
    b, p, u = r.browse, r.capture.profile, r.usage
    stage = " by the text check" if b.stage == "text" else ""
    out = [f"#{r.row_id} {p.name}: {b.decision.upper()}{stage}  ({r.capture_s:.0f} s capture, "
           f"{u.ms / 1000:.0f} s decide, "
           f"{u.input + u.cache_read + u.cache_write} in / {u.output} out tokens, ${u.cost:.3f}"
           + (f", plan 5 h {u.five_hour:.0%}" if u.five_hour is not None else "") + ")"]
    # the whole horizontal row: screenshots only show its first screen
    out.append("  details: " + (" · ".join(f"{l}: {v}" if l else v for l, v in p.details) or "-"))
    out.append(f"  reason: {b.reason}")
    if b.dealbreakers:
        out.append(f"  dealbreakers: {'; '.join(b.dealbreakers)}")
    if b.soft_flags:
        out.append(f"  soft flags: {'; '.join(b.soft_flags)}")
    if b.decision == "like":
        sec = p.find(b.comment_on)
        what = sec.text.replace("\n", " — ") if sec and sec.kind == "prompt" else "(photo)"
        out.append(f"  on {b.comment_on}: {what}")
        c = lib.comment(b.comment)
        if c:
            out.append(f"  comment: {c.id} [{c.lang}] template: {c.text}")
            out.append(f"  sends: {b.comment_text or c.text}")
        else:
            out.append(f"  comment: none; missing: {b.missing_hook}")
    out.append(f"  prompt languages: {' '.join(b.prompt_langs) or '-'} → chat {b.chat_lang}")
    if b.fixes:
        out.append(f"  code fixed: {'; '.join(b.fixes)}")
    out.append("  sections: " + ", ".join(s.ref for s in p.sections))
    return out


def make_label(row: ShadowRow, decision: str, section: str | None, comment: str | None, lib: Library,
               refs: list[str] | None = None, note: str = "", wrong: bool = False, dealbreaker: bool = False,
               sensitive: bool = False, text: str | None = None) -> dict:
    """Dev's choice for a row. `refs` = the captured sections (from the tmpfs capture) when still there. `text` = what
    goes out: Dev's own version of the comment; default the bot's adapted text when Dev takes the bot's comment,
    else the template's own text."""
    if decision not in ("like", "skip"):
        raise LabelError("like or skip")
    if section is not None:
        section = re.sub(r"^(photo|prompt)\W*(\d+)$", r"\1 \2", section.strip().lower())
    bot = row.bot
    if decision == "skip":
        section = comment = "none"
    elif section is None:
        if bot["decision"] != "like":
            raise LabelError("the bot skipped: name the section (and the comment or none)")
        section = bot["comment_on"]
        if comment is None:
            comment = bot["comment"]
            text = text or bot.get("comment_text") or None
    elif comment is None:
        raise LabelError("name the comment id (or none) with the section")
    if decision == "like":
        if refs is not None and section not in refs:
            raise LabelError(f"{section!r} isn't a section of that profile ({', '.join(refs)})")
        if comment != "none" and lib.comment(comment) is None:
            raise LabelError(f"comment {comment!r} isn't in templates.md (add it first)")
    if comment == "none" or comment is None:
        text = None
    elif text is None:
        text = lib.comment(comment).text
    elif (why := refusal(" ".join(text.split()), COMMENT_MAX)) is not None:
        raise LabelError(f"text refused: {why}")
    return {"decision": decision, "comment_on": section, "comment": comment, "note": note, "wrong": wrong,
            "dealbreaker": dealbreaker, "sensitive": sensitive,
            "text": " ".join(text.split()) if text else None}


def _label_text(dev: dict, lib: Library) -> str:
    """What goes out for a like label (labels before #78 had no text: the template's)."""
    if dev.get("text"):
        return dev["text"]
    c = lib.comment(dev["comment"])
    if c is None:
        raise LabelError(f"comment {dev['comment']!r} is gone from templates.md")
    return c.text


def load_capture(tmp_dir: Path, row_id: int) -> Capture | None:
    path = _capture_file(tmp_dir, row_id)
    if not path.exists():
        return None
    return pickle.loads(path.read_bytes())   # own file, 0600 in our 0700 tmpfs dir


def type_label(nav: Navigator, row: ShadowRow, cap: Capture, lib: Library) -> str:
    """Brings the labelled section's comment panel up with the comment typed and read back. Returns the text."""
    dev = row.dev
    if not dev or dev["decision"] != "like" or dev["comment"] == "none":
        raise LabelError("nothing to type: label it as a like with a comment first")
    text = _label_text(dev, lib)
    s, sv = nav.scroll_to(cap, dev["comment_on"])   # checks it's the same profile and section
    nav.open_sheet(s, sv)
    nav.type_comment(text)
    return text


def act_label(nav: Navigator, row: ShadowRow, cap: Capture, lib: Library, action: str) -> tuple[dict, str]:
    """Carries out Dev's label on the phone: `skip` on a skip label, `send` on a like label whose comment `type_label`
    typed (panel still open). Only on the captured profile. Returns the label marked as acted and the next name."""
    dev = row.dev
    if not dev:
        raise LabelError("label the row first: only Dev's decision is carried out")
    if action == "skip":
        if dev["decision"] != "skip":
            raise LabelError("the label isn't a skip")
        nxt = nav.skip_profile(cap.profile.name)
    elif action == "send":
        if dev["decision"] != "like" or dev["comment"] == "none":
            raise LabelError("the label isn't a like with a comment (type it first)")
        nxt = nav.send_like(cap.profile.name, _label_text(dev, lib))
    else:
        raise LabelError("skip or send")
    return dev | {"acted": action}, nxt


def _text_first(rows: list[ShadowRow], usage_rows: list[tuple]) -> list[str]:
    """Does the text check pay? Saved = early skips × a full call's mean cost; spent = all text checks."""
    text = [u[6] or 0 for u in usage_rows if u[0] == TASK_TEXT]
    full = [u[6] or 0 for u in usage_rows if u[0] == TASK_BROWSE]
    if not text or not full:
        return []
    early = sum(r.bot.get("stage") == "text" for r in rows)
    saved, spent = early * statistics.mean(full), sum(text)
    return [f"  text first: {early} of {len(text)} profiles skipped before photos, saved ~${saved:.2f}, the text "
            f"checks cost ${spent:.2f} → {'pays' if saved > spent else 'does not pay (BUDDY_SCREENER_TEXT_FIRST=0)'}"]


def session_rows(rows: list[ShadowRow], usage_rows_since, since_id: int | None) -> tuple[list[ShadowRow], list[tuple]]:
    """One session's rows (id ≥ `since_id`) and the shadow calls (text, browse) made for them: never the sim's, never
    an earlier session's. `usage_rows_since(ts)` = `Store.usage_rows`."""
    if since_id is not None:
        rows = [r for r in rows if r.id >= since_id]
    # a row is stored after its capture and decision: its calls start capture_s + decide_ms before its ts
    start = min((r.ts - r.capture_s - r.decide_ms / 1000 - 1 for r in rows), default=0.0) if since_id is not None else 0.0
    return rows, [u for u in usage_rows_since(start) if u[0] in (TASK_TEXT, TASK_BROWSE)]


def report(rows: list[ShadowRow], usage_rows: list[tuple]) -> list[str]:
    done = [r for r in rows if r.dev]
    out = [f"shadow rows: {len(rows)}, labelled: {len(done)} ({len({r.key for r in done})} profiles)"]
    if done:
        agree = sum(r.bot["decision"] == r.dev["decision"] for r in done)
        bot_like_db = sum(r.bot["decision"] == "like" and r.dev["dealbreaker"] for r in done)
        sensitive = sum(r.dev["sensitive"] for r in done)
        both = [r for r in done if r.bot["decision"] == r.dev["decision"] == "like"]
        same = sum((r.bot["comment_on"], r.bot["comment"]) == (r.dev["comment_on"], r.dev["comment"]) for r in both)
        wrong = sum(r.dev["wrong"] for r in done)
        missing = [r.bot["missing_hook"] for r in done if r.bot["decision"] == "like" and r.bot["missing_hook"]]

        def gate(ok: bool) -> str:
            return "ok" if ok else "NOT YET"
        out += [
            f"  agreement like/skip: {agree}/{len(done)} = {agree / len(done):.0%} "
            f"[{gate(agree / len(done) >= GATE_AGREEMENT and len(done) >= GATE_PROFILES)}: ≥ {GATE_AGREEMENT:.0%} "
            f"over ≥ {GATE_PROFILES}]",
            f"  bot likes where Dev saw a dealbreaker: {bot_like_db} [{gate(bot_like_db == 0)}]",
            f"  sensitive inference in a reason: {sensitive} [{gate(sensitive == 0)}]",
            f"  section + comment as Dev chose: {same}/{len(both)}"
            + (f" = {same / len(both):.0%} [{gate(same / len(both) >= GATE_TEMPLATE)}: ≥ {GATE_TEMPLATE:.0%}]"
               if both else ""),
            f"  comments that read wrong: {wrong} [{gate(wrong == 0)}]",
            f"  likes: bot {sum(r.bot['decision'] == 'like' for r in done)}, Dev "
            f"{sum(r.dev['decision'] == 'like' for r in done)}; no fitting comment (bot): {len(missing)}",
        ]
        out += [f"    missing: {m}" for m in missing[-15:]]
        early = [r for r in done if r.bot.get("stage") == "text"]
        if early:
            out.append(f"  skipped by the text check: {len(early)}, Dev liked "
                       f"{sum(r.dev['decision'] == 'like' for r in early)} of them")
        fixes = sum(bool(r.bot.get("fixes")) for r in done)
        if fixes:
            out.append(f"  decisions code had to fix: {fixes}")
    if rows:
        caps = [r.capture_s for r in rows]
        ms = [r.decide_ms for r in rows]
        out.append(f"  per profile: capture {statistics.median(caps):.0f} s, decide {statistics.median(ms) / 1000:.0f} s "
                   f"(medians), {statistics.mean(r.tokens for r in rows):.0f} tokens, "
                   f"${statistics.mean(r.cost for r in rows):.3f} API-equivalent (means)")
    if usage_rows:
        by_model: dict[str, list[tuple]] = {}
        for u in usage_rows:
            by_model.setdefault(u[1], []).append(u)
        for model, us in by_model.items():
            out.append(f"  {model}: {len(us)} calls, ${sum(u[6] or 0 for u in us):.2f} API-equivalent")
        out += _text_first(rows, usage_rows)
        last = usage_rows[-1]
        if last[8] is not None:
            out.append(f"  plan after the last call: 5 h {last[8]:.0%}"
                       + (f", 7 days {last[9]:.0%}" if last[9] is not None else ""))
    return out
