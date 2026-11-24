"""Step 2 shadow mode: the bot judges the profile on screen, Dev acts by hand and labels (#66 step 2).

A session is a sequence of offline commands (run from Claude's session or a terminal on the host):

1. `shadow`: capture the profile on screen, decide, store the bot's decision, print it (texts: host terminal only).
2. Dev decides on the phone. `label <id> like|skip [section] [comment]`: Dev's choice; a like without section and
   comment takes the bot's. A comment Dev writes themselves goes into `templates.md` first, then gets labelled by id.
   `label <id> unjudged`: Dev couldn't see the profile; `report` leaves the row out, `skip` still works on it.
3. `type <id>` (a like): scroll to the labelled section, open its panel, type the comment (if any) through our
   keyboard and read it back. **Dev taps send**: nothing here sends, skips or likes.
4. `report`: the done-when numbers.

The capture between `shadow` and `type` waits in tmpfs (pickled, without photo crops): `type` must find the same
profile on screen, else it stops.
"""

import pickle
import re
import statistics
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from . import vocab
from .chat import refusal
from .decide import (OWN, TASK_BROWSE, TASK_COMMENT, TASK_COMMENT_CHECK, TASK_PHOTO, TASK_TEXT, Browse, Decider,
                     Usage)
from .library import COMMENT_MAX, Library
from .navigator import Navigator
from .profile import Capture
from .store import ShadowRow, Store, profile_key

# done-when of step 2 (#66)
GATE_PROFILES = 50
GATE_AGREEMENT = 0.85
GATE_TEMPLATE = 0.80


UNJUDGED = "unjudged"   # Dev couldn't see the profile: nothing to measure against


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
    if not p.details:
        # every profile shows a details card (age at least): none = the capture ended early (#165), never judge it
        raise nav.needs_you("no details captured: the capture stopped before the profile's end")
    browse, usage = decider.decide(p, lib)
    first_prompt = next((s.text for s in p.sections if s.kind == "prompt"), "")
    bot = browse.to_dict() | {"chat_lang": browse.chat_lang}
    row_id = store.add_shadow(profile_key(p.name, p.details, first_prompt), capture_s, bot, usage)

    for s in p.sections:
        s.jpeg = s.check_jpeg = b""   # crops never leave memory
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
    stage = {"text": " by the text check", "photo": " by the photo check"}.get(b.stage, "")
    out = [f"#{r.row_id} {p.name}: {b.decision.upper()}{stage}  ({r.capture_s:.0f} s capture, "
           f"{u.ms / 1000:.0f} s decide, "
           f"{u.input + u.cache_read + u.cache_write} in / {u.output} out tokens, ${u.cost:.3f}"
           + (f", plan 5 h {u.five_hour:.0%}" if u.five_hour is not None else "") + ")"]
    # the whole horizontal row: screenshots only show its first screen
    out.append("  details: " + (" · ".join(f"{l}: {v}" if l else v for l, v in p.details) or "-"))
    out.append(f"  reason: {b.reason}")
    if b.photo_check:
        out.append(f"  photo check: {b.photo_check}" + (f" ({b.photo_ref})" if b.photo_ref else ""))
    if b.dealbreakers:
        out.append(f"  dealbreakers: {'; '.join(b.dealbreakers)}")
    if b.soft_flags:
        out.append(f"  soft negatives: {'; '.join(b.soft_flags)}")
    if b.soft_plus:
        out.append(f"  soft positives: {'; '.join(b.soft_plus)}")
    if b.uncovered:
        out.append(f"  preferences silent on: {'; '.join(b.uncovered)}")
    if b.dropped_flags:
        out.append(f"  soft flags dropped by code: {'; '.join(b.dropped_flags)}")
    if b.decision == "like":
        sec = p.find(b.comment_on)
        poll = p.poll_of(sec) if sec and sec.kind == "option" else None
        what = (sec.text.replace("\n", " — ") if sec and sec.kind == "prompt"
                else f"{poll.text if poll else '(poll)'} — {sec.text}" if sec and sec.kind == "option" else "(photo)")
        out.append(f"  on {b.comment_on}: {what}")
        c = lib.comment(b.comment)
        if b.comment_text:
            out.append(f"  sends: {b.comment_text}")
            out.append(f"  leaned on: {c.id}: {c.text}" if c else "  leaned on: no example")
        else:
            out.append(f"  comment: none; missing: {b.missing_hook}")
    if b.comment_refused:
        out.append(f"  comments refused: {'; '.join(b.comment_refused)}")
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
    else the template's own text. Comment `own` (#84): no template, the text is the comment."""
    if decision not in ("like", "skip", UNJUDGED):
        raise LabelError(f"like, skip or {UNJUDGED}")
    if section is not None:
        section = re.sub(r"^(photo|prompt|option)\W*(\d+)$", r"\1 \2", section.strip().lower())
    bot = row.bot
    if decision in ("skip", UNJUDGED):
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
        if comment not in ("none", OWN) and lib.comment(comment) is None:
            raise LabelError(f"comment {comment!r} isn't in templates.md (add it first)")
        if comment == OWN and not text:
            raise LabelError(f"comment {OWN} needs its text (--text, or a bare like on the bot's)")
    if comment == "none" or comment is None:
        text = None
    elif text is None:
        text = lib.comment(comment).text
    elif (why := refusal(" ".join(text.split()), COMMENT_MAX)) is not None:
        raise LabelError(f"text refused: {why}")
    label = {"decision": decision, "comment_on": section, "comment": comment, "note": note, "wrong": wrong,
             "dealbreaker": dealbreaker, "sensitive": sensitive,
             "text": " ".join(text.split()) if text else None}
    if row.dev and row.dev.get("acted"):
        label["acted"] = row.dev["acted"]   # a relabel doesn't undo what happened on the phone
    return label


def _label_text(dev: dict, lib: Library) -> str:
    """What goes out for a like label: "" = no comment (labels before #78 had no text: the template's)."""
    if dev.get("text"):
        return dev["text"]
    if dev["comment"] == "none":
        return ""
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
    if not dev or dev["decision"] != "like":
        raise LabelError("nothing to type: label it as a like first")
    text = _label_text(dev, lib)
    s, sv = nav.scroll_to(cap, dev["comment_on"])   # checks it's the same profile and section
    nav.open_sheet(s, sv)
    if text:   # a like without a comment (#80: none fitted): the panel open, the field empty
        nav.type_comment(text)
    return text


def act_label(nav: Navigator, row: ShadowRow, cap: Capture, lib: Library, action: str) -> tuple[dict, str]:
    """Carries out Dev's label on the phone: `skip` on a skip label, `send` on a like label whose comment `type_label`
    typed (panel still open). Only on the captured profile. Returns the label marked as acted and the next name."""
    dev = row.dev
    if not dev:
        raise LabelError("label the row first: only Dev's decision is carried out")
    if action == "skip":
        if dev["decision"] not in ("skip", UNJUDGED):
            raise LabelError("the label isn't a skip")
        nxt = nav.skip_profile(cap.profile.name)
    elif action == "send":
        if dev["decision"] != "like":
            raise LabelError("the label isn't a like (type it first)")
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


def _photo_check(done: list[ShadowRow]) -> list[str]:
    """Overweight (#83): the photo check's yes against Dev's labels. A miss = Dev's dealbreaker skip where the check
    didn't say yes: the note tells whether weight was the reason. Done when ≤ 1 miss in ≥ 25 profiles."""
    checked = [r for r in done if r.bot.get("photo_check")]
    if not checked:
        return []
    verdicts = Counter(r.bot["photo_check"] for r in checked)
    yes = [r for r in checked if r.bot["photo_check"] == "yes"]
    misses = [r for r in checked if r.bot["photo_check"] != "yes" and r.dev["dealbreaker"]]
    out = [f"  overweight (photo check): yes {verdicts['yes']}, no {verdicts['no']}, can't tell "
           f"{verdicts['cant_tell']} of {len(checked)}; Dev liked {sum(r.dev['decision'] == 'like' for r in yes)} of "
           f"the yes; Dev's dealbreakers without a yes: {len(misses)} (misses if the note says weight)"]
    out += [f"    #{r.id}: {r.dev.get('note') or '-'}" for r in misses]
    return out


def session_rows(rows: list[ShadowRow], usage_rows_since, since_id: int | None,
                 until_id: int | None = None) -> tuple[list[ShadowRow], list[tuple]]:
    """One session's rows (`since_id` ≤ id ≤ `until_id`, either open) and the shadow calls (text, browse) made for
    them: never the sim's, never another session's. `usage_rows_since(from, to)` = `Store.usage_rows`."""
    rows = [r for r in rows if (since_id is None or r.id >= since_id) and (until_id is None or r.id <= until_id)]
    # a row is stored after its capture and decision: its calls start capture_s + decide_ms before its ts
    start = min((r.ts - r.capture_s - r.decide_ms / 1000 - 1 for r in rows), default=0.0) if since_id is not None else 0.0
    end = max((r.ts for r in rows), default=0.0) + 1 if until_id is not None else float("inf")
    tasks = (TASK_TEXT, TASK_PHOTO, TASK_BROWSE, TASK_COMMENT, TASK_COMMENT_CHECK)
    return rows, [u for u in usage_rows_since(start, end) if u[0] in tasks]


def _sent_bots_comment(r: ShadowRow) -> bool:
    """A like both agreed on: did Dev send what the bot proposed? Since #78 the bot adapts a library comment, so the
    same section and the same text going out (a bare `like` copies it); rows from before by section and comment id."""
    bot, dev = r.bot, r.dev
    if bot["comment_on"] != dev["comment_on"]:
        return False
    if "text" in dev and "comment_text" in bot:
        return (dev["text"] or "") == (bot["comment_text"] if bot["comment"] != "none" else "")
    return bot["comment"] == dev["comment"]


EDITED = 0.3   # word overlap (Jaccard) of Dev's text with the bot's: above = Dev edited it, below = wrote their own


def comment_outcome(r: ShadowRow) -> str:
    """A like both agreed on: what Dev did with the bot's comment (#84)."""
    bot, dev = r.bot, r.dev
    if bot["comment_on"] != dev["comment_on"]:
        return "other section"
    if _sent_bots_comment(r):
        return "sent"
    mine, theirs = set(re.findall(r"\w+", (dev.get("text") or "").casefold())), \
        set(re.findall(r"\w+", (bot.get("comment_text") or "").casefold()))
    if not mine:
        return "no comment"
    if theirs and len(mine & theirs) / len(mine | theirs) >= EDITED:
        return "edited"
    return "own"


def report(rows: list[ShadowRow], usage_rows: list[tuple]) -> list[str]:
    unjudged = sum(bool(r.dev) and r.dev["decision"] == UNJUDGED for r in rows)
    done = [r for r in rows if r.dev and r.dev["decision"] != UNJUDGED]
    out = [f"shadow rows: {len(rows)}, labelled: {len(done)} ({len({r.key for r in done})} profiles)"
           + (f", not judged (left out): {unjudged}" if unjudged else "")]
    if done:
        agree = sum(r.bot["decision"] == r.dev["decision"] for r in done)
        bot_like_db = sum(r.bot["decision"] == "like" and r.dev["dealbreaker"] for r in done)
        sensitive = sum(r.dev["sensitive"] for r in done)
        both = [r for r in done if r.bot["decision"] == r.dev["decision"] == "like"]
        same = sum(_sent_bots_comment(r) for r in both)
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
            f"  Dev sent the bot's comment: {same}/{len(both)}"
            + (f" = {same / len(both):.0%} [{gate(same / len(both) >= GATE_TEMPLATE)}: ≥ {GATE_TEMPLATE:.0%}]"
               if both else ""),
            f"  comments that read wrong: {wrong} [{gate(wrong == 0)}]",
            f"  likes: bot {sum(r.bot['decision'] == 'like' for r in done)}, Dev "
            f"{sum(r.dev['decision'] == 'like' for r in done)}; no fitting comment (bot): {len(missing)}",
        ]
        out += [f"    missing: {m}" for m in missing[-15:]]
        if both:
            by_kind: dict[str, Counter[str]] = {}
            for r in both:
                by_kind.setdefault(r.bot["comment_on"].split()[0], Counter())[comment_outcome(r)] += 1
            for kind, c in sorted(by_kind.items()):
                out.append(f"    on a {kind}: " + ", ".join(f"{k} {n}" for k, n in c.most_common()))
        refused = [x for r in done for x in r.bot.get("comment_refused", [])]
        if refused:
            out.append(f"  comments refused by the checks: {len(refused)} ("
                       + ", ".join(f"{k} {n}" for k, n in Counter(x.split()[0] for x in refused).most_common()) + ")")
        early = [r for r in done if r.bot.get("stage") == "text"]
        if early:
            out.append(f"  skipped by the text check: {len(early)}, Dev liked "
                       f"{sum(r.dev['decision'] == 'like' for r in early)} of them")
        out += _photo_check(done)
        fixes = sum(bool(r.bot.get("fixes")) for r in done)
        if fixes:
            out.append(f"  decisions code had to fix: {fixes}")
    flagged = [r for r in rows if r.bot.get("soft_flags") or r.bot.get("soft_plus") or r.bot.get("dropped_flags")]
    if flagged:
        # one line per profile: checked by hand against the preferences' wording (#85)
        out.append(f"  soft flags per profile: {len(flagged)}")
        for r in flagged:
            dev = r.dev["decision"] if r.dev else "-"
            parts = [f"- {f}" for f in r.bot.get("soft_flags", [])] + [f"+ {f}" for f in r.bot.get("soft_plus", [])]
            parts += [f"dropped {f}" for f in r.bot.get("dropped_flags", [])]
            out.append(f"    #{r.id} bot {r.bot['decision']}, Dev {dev}: " + "; ".join(parts))
    gaps = [g for r in rows for g in r.bot.get("uncovered", [])]
    if gaps:
        out.append(f"  preferences silent on (bot): {len(gaps)} in "
                   f"{sum(bool(r.bot.get('uncovered')) for r in rows)} profiles")
        out += [f"    {g}" for g in gaps[-15:]]
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
        photo = [u[6] or 0 for u in usage_rows if u[0] == TASK_PHOTO]
        if photo:
            out.append(f"  photo check: {len(photo)} calls, ${statistics.mean(photo):.3f} per profile")
        last = usage_rows[-1]
        if last[8] is not None:
            out.append(f"  plan after the last call: 5 h {last[8]:.0%}"
                       + (f", 7 days {last[9]:.0%}" if last[9] is not None else ""))
    return out
