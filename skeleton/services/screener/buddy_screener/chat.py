"""The chat flow as a state machine (#66 Flows 2–6, #77). Pure: no I/O, no SDK, no clock of its own.

    new → screening → contact_choice → done
                 ↘ closed (no match, opt-out, no reply)   handover (Dev takes over)   manual (Dev typed)

A driver (the sim now, the live loop after step 3) runs it:

    act = flow.step(chat, screen, now)
    send     → type and send `act.text`, then `flow.delivered(chat, act, now)`
    converse → the converse call on `act.transcript` (+ `act.task`), then `flow.conversed(chat, answer, now)`
    verify   → the verifier on `act.transcript`, then `flow.verified(chat, verdict, now)`
    unmatch  → unmatch, then `flow.unmatched(chat, now)`
    notify   → tell Dev (handover / manual: paraphrased, no name, no text)
    wait     → nothing to do until the next message or tick

Everything that matters is decided here, by code, never by the model: the disclosure first, the contact only after
the code checks and the verifier, only the template the match picked (filled here, never generated), every
generated text checked before it goes out, a message of ours that isn't in our log → `manual` for good. The
owner's screening questions are his words: the model only names one (`ask`), code appends it verbatim, attributed to
him by the `question` template (Dev, 2026-11-19: never rephrased).
"""

import re
from dataclasses import dataclass, field
from typing import Any

from .library import ChatRules, Contacts, fill

NEW, SCREENING, CONTACT_CHOICE = "new", "screening", "contact_choice"
DONE, CLOSED, HANDOVER, MANUAL = "done", "closed", "handover", "manual"
OPEN = (NEW, SCREENING, CONTACT_CHOICE)
LIKED = "liked"      # our like went out, no match yet: kept for its chat (profile, comment), 30 days, never stepped
FOREIGN = "foreign"  # a match the bot doesn't take (Dev's own chat, or no unique like to link it to): never touched

THEM, US = "them", "us"
CONTACT_KINDS = ("contact_number", "contact_instagram")
TASK_FIRST, TASK_REPLY, TASK_NUDGE, TASK_OPENER = "first", "reply", "nudge", "opener"

_DIGITS = re.compile(r"\d(?:[\s\-./()]*\d){5,}")      # a phone number, however spaced
_HANDLE = re.compile(r"(?<![\w.])@\w")
_LINK = re.compile(r"https?://[^\s\"')<>]+|\bwww\.\S+", re.I)
_SPACE = re.compile(r"\s+")
# a message that needs no answer before the introduction: greetings, laughs, emojis, punctuation (Dev, 2026-11-24)
_SMALL_TALK = re.compile(
    r"(?:\s|[\W_]|hi+|hey+|hel+o+|hallo+|hoi+|sali+|salü|servus|grüezi|gr[üu]ess? ?di|moin|yo+|na|ha(?:ha)*|he(?:he)*"
    r"|lol|:\)|:d|;\))*", re.I)


def refusal(text: str, limit: int, contacts=(), urls=()) -> str | None:
    """Why a generated text must not go out (None = fine): code's last line, whatever the model was told. Used for
    chat messages and adapted like comments."""
    if not text:
        return "empty"
    if len(text) > limit:
        return f"{len(text)} chars > {limit}"
    flat = re.sub(r"\D", "", text)
    low = text.lower()
    for v in contacts:
        digits = re.sub(r"\D", "", v)
        if (len(digits) >= 6 and digits in flat) or v.lower().lstrip("@") in low:
            return "a contact value"
    if _DIGITS.search(text):
        return "a number"
    if _HANDLE.search(text):
        return "a handle"
    if any(u not in urls for u in _LINK.findall(text)):
        return "a link"
    return None


@dataclass(frozen=True)
class ChatLimits:
    min_exchanges: int = 3       # her answered turns before a match can be verified
    max_bot: int = 12            # our generated messages; at the cap → handover
    max_days: float = 7          # open this long without a decision → handover
    nudge_days: float = 2        # one nudge after this long without a reply (0 = off)
    close_days: float = 3        # no reply this long after the nudge → closed silently
    unmatch_h: float = 24        # unmatch this long after closing
    reply_max: int = 400         # chars of a generated message
    fit_max: int = 100           # chars of the generated fit line ("You two seem like a good fit." ≈ 30)


@dataclass
class Message:
    """One message as the screen (or the sim) shows it."""
    side: str                    # THEM | US
    text: str
    media: bool = False          # a photo, voice note, sticker, …: we can't read it → handover


@dataclass
class Sent:
    ts: float
    kind: str                    # like_comment | opener | disclosure | reply | nudge | contact_offer | contact_* | closing | opt_out | dev
    text: str
    dev: bool = False            # Dev's wording: his edit of a held message, or (kind `dev`) his own message adopted


@dataclass
class Converse:
    """The converse call's answer, already re-checked by `converse.check_converse`."""
    reply: str
    state: str                   # screening | match | no_match | handover
    opt_out: bool = False
    fit_line: str = ""
    answers: dict[int, str | None] = field(default_factory=dict)
    flags: list[str] = field(default_factory=list)
    soft_flags: list[str] = field(default_factory=list)
    contact: str = "none"        # phone | instagram | both | unclear | none
    lang: str = ""               # her latest message: de-ch | de | en | other
    reason: str = ""
    ask: int = 0                 # a screening question to append word for word (code), 0 = none


@dataclass
class Verdict:
    agree: bool
    reason: str = ""


@dataclass
class Chat:
    key: str                     # live: name + thumbnail hash; sim: the case id
    name: str
    started: float
    lang: str = "de"             # "de" | "en": templates and replies
    profile: str = ""            # her details + prompt answers as text (for personal questions)
    state: str = NEW
    answers: dict[int, str] = field(default_factory=dict)
    flags: list[str] = field(default_factory=list)
    soft_flags: list[str] = field(default_factory=list)
    sent: list[Sent] = field(default_factory=list)
    theirs_seen: int = 0         # her messages handled so far
    pending_seen: int = 0        # … once the running converse call is through
    pending_task: str = ""
    pending_reply: str = ""      # the reply to send if the verifier disagrees
    pending_ask: int = 0         # … and the question it asks
    pending_fit: str = ""
    turns: int = 0               # her turns answered (exchanges)
    unclear: int = 0             # unclear contact answers so far
    verified_at: float | None = None
    closed_at: float | None = None
    unmatch_at: float | None = None
    unmatched: bool = False
    reason: str = ""             # why it's closed / handed over / manual (no message text)
    log: list[str] = field(default_factory=list)   # decisions, no message text (the sim's report reads it)
    like_comment: str = ""       # our like's comment as sent ("" = a like without one)
    liked_kind: str = ""         # the liked section: photo | prompt (the chat's head shows it)
    prompt_langs: list[str] = field(default_factory=list)   # the decision's, for the first message's language
    held: dict[str, str] = field(default_factory=dict)      # a message typed and held for Dev (or an unmatch), #77
    manual_from: str = ""        # the state before `manual`: where `give_back` returns to (#88)

    @property
    def bot_msgs(self) -> int:
        return sum(s.kind in ("opener", "reply", "nudge") for s in self.sent)

    @property
    def last_out(self) -> float:
        return max((s.ts for s in self.sent), default=self.started)

    def to_dict(self) -> dict[str, Any]:
        d = dict(self.__dict__)
        d["sent"] = [s.__dict__ for s in self.sent]
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Chat":
        d = dict(d)
        d["sent"] = [Sent(**s) for s in d.get("sent", [])]
        d["answers"] = {int(k): v for k, v in d.get("answers", {}).items()}
        return cls(**d)


@dataclass
class Action:
    kind: str                    # send | converse | verify | unmatch | notify | wait
    template: str = ""           # send: the Sent kind
    text: str = ""               # send: the message; notify: what happened (no name, no message text)
    dev: bool = False            # send: Dev's wording (his edit of the held message)
    task: str = ""               # converse: first | reply | nudge
    transcript: list[tuple[str, str]] = field(default_factory=list)   # converse/verify: (side, text), contacts redacted


WAIT = Action("wait")


def first_lang(prompt_langs: list[str]) -> str:
    """The first message's language from the profile's prompts: the majority of German (incl. Swiss German) vs
    English; a tie → German; only other languages (or none) → English (Dev, 2026-11-18)."""
    de = sum(x in ("de", "de-ch") for x in prompt_langs)
    en = sum(x == "en" for x in prompt_langs)
    if de == en == 0:
        return "de" if not prompt_langs else "en"
    return "de" if de >= en else "en"


_WORD = re.compile(r"[a-zäöüß']+")
_DE = {"und", "ich", "du", "dich", "dir", "ist", "nicht", "was", "wie", "dein", "deine", "hast", "bist", "schön",
       "gern", "gerne", "mit", "auf", "für", "das", "der", "die", "ein", "eine", "magst", "noch", "auch", "sehr",
       # Swiss German (an old match's prompts: no decision to take the languages from, #77)
       "isch", "esch", "ned", "nöd", "mini", "min", "dini", "vo", "ond", "aber", "oder", "mir", "mi", "bi", "gsi",
       "au", "chan", "chasch", "wenn", "dass", "zum", "im", "es", "mis", "dis", "gärn", "öppis", "nüt"}
_EN = {"and", "i", "you", "is", "not", "what", "how", "your", "have", "are", "nice", "with", "for", "the", "a",
       "do", "to", "love", "like", "would", "that", "it's", "so", "really", "my", "me", "of", "in", "on", "or",
       "but", "if", "when", "be", "can", "i'm", "am", "will", "just"}


def guess_lang(text: str) -> str:
    """German (incl. Swiss German) or English by function words; "" when it can't tell."""
    words = _WORD.findall(text.lower())
    de, en = sum(w in _DE for w in words), sum(w in _EN for w in words)
    return "de" if de > en else "en" if en > de else ""


def chat_lang(lang: str, current: str) -> str:
    """Per her latest message: German or Swiss German → German, English and anything else → English."""
    if lang in ("de", "de-ch"):
        return "de"
    if lang in ("en", "other"):
        return "en"
    return current


def _norm(text: str) -> str:
    return _SPACE.sub(" ", text).strip()


class ChatFlow:
    def __init__(self, rules: ChatRules, contacts: Contacts, limits: ChatLimits = ChatLimits()):
        self.rules, self.contacts, self.limits = rules, contacts, limits
        self._screens: dict[str, list[Message]] = {}   # per chat: the screen its running converse call saw

    # --- the driver's entry points -------------------------------------------------------------------------------

    def open(self, chat: Chat, like_comment: str, prompt_langs: list[str], now: float) -> None:
        """A new match: our like comment (if any) is ours, not Dev's typing; the first message's language."""
        if like_comment:
            chat.sent.append(Sent(now, "like_comment", like_comment))
        chat.lang = first_lang(prompt_langs)
        chat.log.append(f"open lang={chat.lang}")

    def step(self, chat: Chat, screen: list[Message], now: float) -> Action:
        if chat.state == MANUAL:
            return WAIT
        if (typed := self._typed_by_dev(chat, screen)) is not None:
            return self._end(chat, MANUAL, now, f"a message of ours isn't in the log ({typed})")
        if chat.state in (DONE, HANDOVER):
            return WAIT
        if chat.state == CLOSED:
            if not chat.unmatched and chat.unmatch_at is not None and now >= chat.unmatch_at:
                return Action("unmatch")
            return WAIT
        theirs = [m for m in screen if m.side == THEM]
        new = theirs[chat.theirs_seen:]
        if any(m.media for m in new):
            return self._end(chat, HANDOVER, now, "she sent media or a voice note")
        if not any(s.kind == "disclosure" for s in chat.sent):
            # she answered our like comment before we introduced ourselves: answer her first, then the
            # introduction (Dev, 2026-11-24); Dev's own messages (given back) already answered her
            if theirs and not all(_SMALL_TALK.fullmatch(m.text) for m in theirs) \
                    and not any(s.kind in ("opener", "dev") for s in chat.sent) and chat.pending_task != TASK_OPENER:
                return self._converse(chat, screen, TASK_OPENER, len(theirs))
            return self._send(chat, "disclosure", fill(self._t("disclosure", chat), name=chat.name))
        if now - chat.started >= self.limits.max_days * 86400 and chat.state in (NEW, SCREENING):
            return self._end(chat, HANDOVER, now, f"no decision after {self.limits.max_days:g} days")
        if chat.state == NEW:
            # after the introduction we wait for her: in or out (Dev, 2026-11-24). Only her messages after it count
            if self._after_disclosure(chat, screen):
                return self._converse(chat, screen, TASK_FIRST, len(theirs))
            return self._tick(chat, screen, now)
        if new:
            return self._converse(chat, screen, TASK_REPLY, len(theirs))
        return self._tick(chat, screen, now)

    def delivered(self, chat: Chat, act: Action, now: float) -> None:
        chat.sent.append(Sent(now, act.template, act.text, act.dev))
        k = act.template
        if k == "reply" and chat.state == NEW:
            chat.state = SCREENING
        elif k == "contact_offer":
            chat.state = CONTACT_CHOICE
        elif k in CONTACT_KINDS:
            chat.state = DONE
            chat.reason = k
        elif k in ("closing", "opt_out"):
            self._close(chat, now, k)
        chat.log.append(f"sent {k}")

    def conversed(self, chat: Chat, c: Converse, now: float) -> Action:
        task, chat.pending_task = chat.pending_task, ""
        screen = self._screens.get(chat.key, [])
        new = [m for m in screen if m.side == THEM][chat.theirs_seen:chat.pending_seen]
        chat.theirs_seen = chat.pending_seen
        # her language counts once she writes sentences: "Hello hello" on a Swiss German profile stays German
        # (Dev, 2026-11-24)
        if not new or not all(_SMALL_TALK.fullmatch(m.text) for m in new):
            chat.lang = chat_lang(c.lang, chat.lang)
        for n, a in c.answers.items():
            if a:
                chat.answers[n] = a
        chat.flags += [f for f in c.flags if f not in chat.flags]
        chat.soft_flags += [f for f in c.soft_flags if f not in chat.soft_flags]
        chat.log.append(f"converse {task}: {c.state}{' opt_out' if c.opt_out else ''} contact={c.contact} "
                        f"lang={c.lang} — {c.reason}")
        if c.opt_out:
            return self._send(chat, "opt_out", fill(self._t("opt_out", chat), name=chat.name))
        if c.state == "handover":
            return self._end(chat, HANDOVER, now, f"model: {c.reason}")
        if task == TASK_OPENER:
            if not c.reply.strip():   # nothing to answer (a greeting): the introduction alone
                return self._send(chat, "disclosure", fill(self._t("disclosure", chat), name=chat.name))
            return self._generated(chat, now, "opener", c.reply)
        if task == TASK_NUDGE:
            return self._generated(chat, now, "nudge", c.reply, c.ask)
        if task == TASK_REPLY:
            chat.turns += 1
        if chat.state == CONTACT_CHOICE:
            return self._contact(chat, c, now)
        if c.state == "no_match":
            return self._send(chat, "closing", fill(self._t("closing", chat), name=chat.name))
        if c.state == "match":
            if (why := self._not_ready(chat)) is None:
                chat.pending_reply, chat.pending_ask, chat.pending_fit = c.reply, c.ask, c.fit_line
                return Action("verify", transcript=self.transcript(chat, self._screens.pop(chat.key, [])))
            chat.log.append(f"match refused by code: {why}")
        return self._generated(chat, now, "reply", c.reply, c.ask)

    def verified(self, chat: Chat, v: Verdict, now: float) -> Action:
        reply, ask, fit = chat.pending_reply, chat.pending_ask, chat.pending_fit
        chat.pending_reply = chat.pending_fit = ""
        chat.pending_ask = 0
        chat.log.append(f"verify: {'agree' if v.agree else 'disagree'} — {v.reason}")
        if not v.agree:
            return self._generated(chat, now, "reply", reply, ask)
        fit = _norm(fit)
        if (why := self._refuse(fit, self.limits.fit_max)) is not None:
            return self._end(chat, HANDOVER, now, f"fit line refused: {why}")
        chat.verified_at = now
        return self._send(chat, "contact_offer", fill(self._t("contact_offer", chat), name=chat.name, fit_line=fit))

    def unmatched(self, chat: Chat, now: float) -> None:
        chat.unmatched = True
        chat.log.append("unmatched")

    def give_back(self, chat: Chat, screen: list[Message], now: float) -> str | None:
        """Dev hands a `manual` chat back to the bot (#88): his messages on our side join the log as `dev`, the
        state returns to where it was (an old chat without `manual_from`: screening once she answered the
        introduction, else new). Returns why it can't (None = done)."""
        if chat.state != MANUAL:
            return f"the chat is {chat.state}, not manual"
        if any(m.side == US and m.media for m in screen):
            return "our side has media the bot can't read"
        ours = {_norm(s.text) for s in chat.sent}
        for m in screen:
            if m.side == US and _norm(m.text) not in ours:
                chat.sent.append(Sent(now, "dev", m.text, True))
                ours.add(_norm(m.text))
        if chat.manual_from in OPEN:
            chat.state = chat.manual_from
        else:
            chat.state = SCREENING if self._after_disclosure(chat, screen) else NEW
        chat.theirs_seen = min(chat.theirs_seen, sum(m.side == THEM for m in screen))
        chat.manual_from, chat.reason, chat.closed_at = "", "", None
        chat.log.append(f"given back by Dev: {chat.state}")
        return None

    # --- transcript ----------------------------------------------------------------------------------------------

    def transcript(self, chat: Chat, screen: list[Message]) -> list[tuple[str, str]]:
        """What the converse/verify calls see: the chat as on screen, our contact messages redacted (a contact value
        never reaches a prompt, even after it was sent)."""
        contacts = {_norm(s.text) for s in chat.sent if s.kind in CONTACT_KINDS}
        out = []
        for m in screen:
            if m.side == US and _norm(m.text) in contacts:
                out.append((US, "[contact sent]"))
            elif m.media:
                out.append((m.side, "[media]"))
            else:
                out.append((m.side, m.text))
        return out

    # --- internals -----------------------------------------------------------------------------------------------

    def _t(self, name: str, chat: Chat) -> str:
        return self.rules.templates[name][chat.lang]

    def _typed_by_dev(self, chat: Chat, screen: list[Message]) -> str | None:
        ours = {_norm(s.text) for s in chat.sent}
        for i, m in enumerate(screen):
            if m.side == US and (m.media or _norm(m.text) not in ours):
                return f"message {i + 1}"
        return None

    def _converse(self, chat: Chat, screen: list[Message], task: str, seen: int) -> Action:
        chat.pending_task, chat.pending_seen = task, seen
        self._screens[chat.key] = screen
        return Action("converse", task=task, transcript=self.transcript(chat, screen))

    def _after_disclosure(self, chat: Chat, screen: list[Message]) -> bool:
        """Has she written since our introduction? (Her messages before it were answered by the opener.)"""
        intro = next((_norm(s.text) for s in chat.sent if s.kind == "disclosure"), None)
        at = max((i for i, m in enumerate(screen) if m.side == US and _norm(m.text) == intro), default=None)
        return at is not None and any(m.side == THEM for m in screen[at + 1:])

    def _tick(self, chat: Chat, screen: list[Message], now: float) -> Action:
        lim, day = self.limits, 86400
        if chat.sent and chat.sent[-1].kind == "disclosure":
            # no answer to the introduction: no nudge (she hasn't opted in), closed silently after close_days
            if now - chat.last_out >= lim.close_days * day:
                chat.log.append("no answer to the introduction: closed silently")
                self._close(chat, now, "no_reply")
            return WAIT
        if not chat.sent or chat.sent[-1].kind not in ("reply", "nudge", "contact_offer", "dev"):
            return WAIT
        quiet = now - chat.last_out
        nudged = chat.sent[-1].kind == "nudge"
        if nudged and quiet >= lim.close_days * day:
            chat.log.append("no reply after the nudge: closed silently")
            self._close(chat, now, "no_reply")
            return WAIT
        if not nudged and lim.nudge_days > 0 and quiet >= lim.nudge_days * day \
                and not any(s.kind == "nudge" for s in chat.sent):
            return self._converse(chat, screen, TASK_NUDGE, chat.theirs_seen)
        return WAIT

    def _not_ready(self, chat: Chat) -> str | None:
        if missing := [n for n in self.rules.required if not chat.answers.get(n)]:
            return f"required question(s) {missing} unanswered"
        if chat.flags:
            return f"dealbreaker flag(s): {len(chat.flags)}"
        if chat.turns < self.limits.min_exchanges:
            return f"{chat.turns} exchanges < {self.limits.min_exchanges}"
        return None

    def _contact(self, chat: Chat, c: Converse, now: float) -> Action:
        if c.state == "no_match" or c.contact == "none":
            return self._end(chat, HANDOVER, now, f"contact choice: {c.contact}, {c.state}")
        if c.contact in ("phone", "both"):     # both → the number only (Dev, 2026-11-19)
            return self._send(chat, "contact_number",
                              fill(self._t("contact_number", chat), name=chat.name, number=self.contacts.number))
        if c.contact == "instagram":
            return self._send(chat, "contact_instagram", fill(self._t("contact_instagram", chat), name=chat.name,
                                                              instagram=self.contacts.instagram))
        chat.unclear += 1
        if chat.unclear > 1:
            return self._end(chat, HANDOVER, now, "contact choice unclear twice")
        return self._generated(chat, now, "reply", c.reply)

    def _generated(self, chat: Chat, now: float, kind: str, text: str, ask: int = 0) -> Action:
        """The model's text, checked; with `ask` the owner's question appended verbatim (only the model's part is
        checked: the question is his, links in it included)."""
        if chat.bot_msgs >= self.limits.max_bot:
            return self._end(chat, HANDOVER, now, f"{self.limits.max_bot} bot messages without a decision")
        text = text.strip()
        q = self.rules.question(ask) if ask else None
        if not (q and not text) and (why := self._refuse(text, self.limits.reply_max)) is not None:
            return self._end(chat, HANDOVER, now, f"{kind} refused: {why}")
        if q:
            asked = fill(self._t("question", chat), name=chat.name, question=q.in_lang(chat.lang))
            text = f"{text} {asked}" if text else asked
            chat.log.append(f"asks question {q.n}")
        return self._send(chat, kind, text)

    def _refuse(self, text: str, limit: int) -> str | None:
        return refusal(text, limit, self.contacts.values(), self.rules.urls)

    def _send(self, chat: Chat, kind: str, text: str) -> Action:
        return Action("send", template=kind, text=text)

    def _close(self, chat: Chat, now: float, why: str) -> None:
        chat.state, chat.reason, chat.closed_at = CLOSED, why, now
        chat.unmatch_at = now + self.limits.unmatch_h * 3600

    def _end(self, chat: Chat, state: str, now: float, why: str) -> Action:
        if state == MANUAL:
            chat.manual_from = chat.state
        chat.state, chat.reason = state, why
        chat.closed_at = now
        chat.log.append(f"{state}: {why}")
        return Action("notify", text=f"{state}: {why}")
