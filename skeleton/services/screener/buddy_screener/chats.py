"""The chats on the phone (#77): the live twin of `sim.run_chat`. One `ChatDriver.step()` = one pass over the matches
list, or the resume of a message Dev was asked to read.

Modes (`BUDDY_SCREENER_CHAT`): `off`, `hold` (every message is typed into the chat's field, not sent, and Dev is asked
on Telegram, without the text: Dev reads it on the phone; Resume = read back + send; Dev changing or sending it
himself makes the chat `manual`, Dev 2026-11-24) and `live` (sent at once). An unmatch is held the same way in `hold`.

Which chats (Dev, 2026-11-24): a match linked to one of our `liked` chats (the same name, exactly one, its head fits),
or an old match where we sent nothing but the like (her profile from the chat's Profile tab). Names in the host's
`chat-ignore` and everything else (`foreign`) are never touched. A pass runs when the tab bar's matches badge counts
a new message, or every `check_s` for new matches and timers (nudge, silent close, unmatch); a chat is opened only
when she wrote, it's new, or a timer is due. Messages go out as soon as seen (Dev: no delay, no hours).

Telegram gets fixed texts per kind only: never a name, a message or the model's reason (bot chats aren't end-to-end
encrypted, D22).
"""

import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from . import screens
from . import vocab as v
from .chat import (CLOSED, DONE, FOREIGN, HANDOVER, LIKED, MANUAL, NEW, OPEN, THEM, US, Action, Chat, ChatFlow,
                   ChatLimits, Message, guess_lang)
from .decide import BudgetSpent, DecideError, UsageLimited, profile_text
from .library import ChatRules, Contacts, LibraryError
from .navigator import ChatRow, Navigator, NeedsYou, same_chat
from .store import Store

OFF, HOLD, LIVE = "off", "hold", "live"

HOLD_CHAT_MESSAGE = "chat_message"   # a message typed, not sent: Dev reads it on the phone
HOLD_CHAT_UNMATCH = "chat_unmatch"   # a closed chat's unmatch waits for Dev's OK
HOLD_CHAT_SCREEN = "chat_screen"     # the phone showed something unexpected in a chat
HOLD_CHAT_FILES = "chat_files"       # chat.md / templates / contact files refused

FAILURES = 3          # converse/verify failing in a row on one chat → handover
MAX_ACTS = 12         # actions per chat per pass (a chat can't loop)
DAY = 86400.0

# what Dev is told when a chat ends up somewhere (fixed: no name, no text, no model reason)
NOTICE = {
    HANDOVER: "a chat needs you: the bot handed it over.\nTo do: open the matches tab and take that chat over.",
    MANUAL: "a chat is yours now: a message on our side wasn't the bot's.\nTo do: nothing, the bot stays out of it.",
    DONE: "a chat got your contact after a verified match.\nTo do: expect a message from her.",
    CLOSED: "a chat was closed (no fit or she opted out); the unmatch follows in a day.\nTo do: nothing.",
    FOREIGN: "a match the bot leaves to you (no like of its own to link it to).\nTo do: chat in the app yourself.",
}


@dataclass(frozen=True)
class ChatConfig:
    mode: str = OFF
    check_s: float = 600.0   # BUDDY_SCREENER_CHAT_CHECK_MIN


def read_ignore(path: Path) -> set[str]:
    """Host-only names the bot never touches (`config/screener/chat-ignore`, one per line, '#' comments)."""
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return set()
    return {x.split("#", 1)[0].strip() for x in lines if x.split("#", 1)[0].strip()}


def to_flow(msgs: list[screens.Message], origin: screens.Origin) -> list[Message]:
    """The screen's messages as the flow sees them; our like comment (the chat's head) first: her reply answers it."""
    out = [Message(US, origin.comment)] if origin.comment else []
    return out + [Message(THEM if m.side == "left" else US, m.text, m.media) for m in msgs]


class ChatDriver:
    def __init__(self, cfg: ChatConfig, nav: Navigator, talker, store: Store, config_dir: Path,
                 hold: Callable[[str, int | None, str], None], notify: Callable[[str], None],
                 mode: Callable[[], str], limits: ChatLimits = ChatLimits(), clock: Callable[[], float] = time.time,
                 log: Callable[[str], None] = print):
        self.cfg, self.nav, self.talker, self.store, self.config_dir = cfg, nav, talker, store, config_dir
        self.hold, self.notify, self.mode, self.limits, self.clock, self.log = hold, notify, mode, limits, clock, log
        self.next_pass = 0.0
        self.failures: dict[str, int] = {}
        self.until = 0.0   # usage limit / $ cap: no calls before

    # --- schedule ---

    def _badge(self) -> int:
        s = self.nav.last
        tab = s.tabs.get(v.TAB_MATCHES) if s is not None else None
        return tab.badge if tab is not None else 0

    def due(self) -> bool:
        now = self.clock()
        return now >= self.until and (now >= self.next_pass or self._badge() > 0 or self._held() is not None)

    def _held(self) -> Chat | None:
        return next((c for c in self.store.chats(OPEN + (CLOSED,)) if c.held), None)

    def step(self) -> float:
        """One pass (or a held message's resume). Returns seconds until the next pass is due."""
        if self.cfg.mode == OFF:
            return 3600.0
        if self.mode() != "active":
            return 5.0
        now = self.clock()
        if now < self.until:
            return self.until - now
        try:
            rules, contacts = ChatRules.load(self.config_dir), Contacts.load(self.config_dir)
        except LibraryError as e:
            return self._hold(HOLD_CHAT_FILES, f"chat files: {e}")
        flow = ChatFlow(rules, contacts, self.limits)
        try:
            if (held := self._held()) is not None:
                if not self._resume(held, flow):
                    return 5.0
            elif not self.due():
                return max(self.next_pass - now, 1.0)
            self._pass(flow, rules)
            self.nav.go_tab(v.TAB_DISCOVER)
        except NeedsYou as e:
            return self._hold(HOLD_CHAT_SCREEN, e.reason)
        except _Held:
            return 5.0
        except _Limited as e:
            self.until = e.until
            return max(e.until - self.clock(), 1.0)
        self.next_pass = self.clock() + self.cfg.check_s
        return self.cfg.check_s

    def _hold(self, kind: str, why: str) -> float:
        self.store.add_action("needs_you", "chat", None, why[:120])
        self.log(f"chat hold ({kind}): {why}")
        self.hold(kind, None, why)
        return 5.0

    # --- a pass ---

    def _pass(self, flow: ChatFlow, rules: ChatRules) -> None:
        ignore = read_ignore(self.config_dir / "chat-ignore")
        for row in self.nav.matches():
            if row.name in ignore:
                continue
            chat = self._known(row)
            if chat is not None and not self._wants(chat, row):
                continue
            self.nav.open_chat(row.key)
            msgs, origin = self.nav.read_chat()
            if chat is None:
                chat = self._link(row, msgs, origin, flow)
                if chat is None:
                    self._leave()
                    continue
            self._run(chat, flow, rules, to_flow(msgs, origin))
            self._leave()

    def _leave(self) -> None:
        """Out of the chat, if still in it (an unmatch already left it)."""
        if self.nav.stable().kind in (screens.Kind.CHAT, screens.Kind.CHAT_PROFILE):
            self.nav.leave_chat()

    def _known(self, row: ChatRow) -> Chat | None:
        """The chat of this row: by its key, or the same name with a thumbnail hash a few bits off (a crop can shift
        by a pixel between passes: a known chat must never look new, it would turn `foreign`)."""
        if (chat := self.store.chat(row.key)) is not None:
            return chat
        near = [c for c in self.store.chats() if c.state != LIKED and same_chat(c.key, row.key)]
        return near[0] if len(near) == 1 else None

    def _wants(self, chat: Chat, row: ChatRow) -> bool:
        """Open this chat? She wrote, or a timer is due. Ended chats never."""
        now, lim = self.clock(), self.limits
        if chat.state == CLOSED:
            return not chat.unmatched and chat.unmatch_at is not None and now >= chat.unmatch_at
        if chat.state not in OPEN:
            return False
        if row.section == screens.YOUR_TURN or chat.state == NEW:
            return True
        quiet = now - chat.last_out
        nudged = bool(chat.sent) and chat.sent[-1].kind == "nudge"
        return (now - chat.started >= lim.max_days * DAY
                or (nudged and quiet >= lim.close_days * DAY)
                or (not nudged and lim.nudge_days > 0 and quiet >= lim.nudge_days * DAY))

    def _link(self, row: ChatRow, msgs: list[screens.Message], origin: screens.Origin, flow: ChatFlow) -> Chat | None:
        """A match seen for the first time: one of our likes, an old match we only liked, or not ours."""
        now = self.clock()
        cands = [c for c in self.store.liked(row.name)
                 if (not origin.kind or c.liked_kind == origin.kind)
                 and (not origin.comment or " ".join(c.like_comment.split()) == " ".join(origin.comment.split()))]
        ours = [m for m in msgs if m.side == "right"]
        if len(cands) == 1 and not ours:
            chat = cands[0]
            self.store.drop_chat(chat.key)
            chat.key, chat.state, chat.started = row.key, NEW, now
            flow.open(chat, chat.like_comment, chat.prompt_langs, now)
            chat.log.append("linked to our like")
        elif not self.store.liked(row.name) and not ours:
            cap = self.nav.chat_profile()
            langs = [guess_lang(s.text.partition("\n")[2]) or "other" for s in cap.profile.sections
                     if s.kind == "prompt"]
            chat = Chat(row.key, row.name, now, profile=profile_text(cap.profile), like_comment=origin.comment,
                        liked_kind=origin.kind, prompt_langs=langs)
            flow.open(chat, origin.comment, langs, now)
            chat.log.append("an old match: profile from the chat's Profile tab")
        else:
            why = "our side has messages" if ours else f"{len(cands)} of our likes fit"
            chat = Chat(row.key, row.name, now, state=FOREIGN, reason=why, closed_at=now)
            self.store.save_chat(chat)
            self.notify(NOTICE[FOREIGN])
            self.log(f"chat left to Dev: {why}")
            return None
        self.store.save_chat(chat)
        return chat

    def _run(self, chat: Chat, flow: ChatFlow, rules: ChatRules, screen: list[Message]) -> None:
        """The flow on this chat until it waits (or a hold): calls, sends, notices, unmatch."""
        before = chat.state
        for _ in range(MAX_ACTS):
            now = self.clock()
            act = flow.step(chat, screen, now)
            act = self._calls(chat, flow, rules, act)
            if act.kind == "send":
                if self.cfg.mode == HOLD:
                    self.nav.type_message(act.text)
                    chat.held = {"kind": "send", "template": act.template, "text": act.text}
                    self.store.save_chat(chat)
                    self.hold(HOLD_CHAT_MESSAGE, None, act.template)
                    raise _Held
                self.nav.type_message(act.text)
                self.nav.send_message(act.text)
                flow.delivered(chat, act, now)
                screen.append(Message(US, act.text))
            elif act.kind == "unmatch":
                if self.cfg.mode == HOLD:
                    chat.held = {"kind": "unmatch"}
                    self.store.save_chat(chat)
                    self.hold(HOLD_CHAT_UNMATCH, None, "unmatch")
                    raise _Held
                self.nav.unmatch()
                flow.unmatched(chat, now)
                self.store.save_chat(chat)
                return
            elif act.kind == "notify":
                pass   # the end state's fixed notice below, never the flow's text (it carries the model's reason)
            else:
                break
            self.store.save_chat(chat)
        self.store.save_chat(chat)
        if chat.state != before and chat.state in NOTICE:
            self.notify(NOTICE[chat.state])

    def _calls(self, chat: Chat, flow: ChatFlow, rules: ChatRules, act: Action) -> Action:
        """converse / verify until the flow wants something else. Failures: 3 in a row on a chat → handover."""
        while act.kind in ("converse", "verify"):
            try:
                if act.kind == "converse":
                    c, fixes, _ = self.talker.converse(rules, act.task, chat.state, chat.lang, chat.profile,
                                                       sorted(chat.answers), act.transcript)
                    if fixes:
                        chat.log.append(f"fixes: {len(fixes)}")
                    act = flow.conversed(chat, c, self.clock())
                else:
                    verdict, _ = self.talker.verify(rules, chat.profile, act.transcript)
                    act = flow.verified(chat, verdict, self.clock())
                self.failures.pop(chat.key, None)
            except UsageLimited as e:
                self.store.save_chat(chat)
                raise _Limited(e.until) from None
            except BudgetSpent:
                self.store.save_chat(chat)
                raise _Limited(self.clock() + 1800) from None
            except DecideError as e:
                n = self.failures[chat.key] = self.failures.get(chat.key, 0) + 1
                self.log(f"chat call failed ({n}): {type(e).__name__}")
                chat.pending_task = ""
                if n < FAILURES:
                    return Action("wait")
                self.failures.pop(chat.key, None)
                chat.state, chat.reason, chat.closed_at = HANDOVER, f"{FAILURES} failed calls", self.clock()
                return Action("wait")
        return act

    # --- Dev's answer to a hold ---

    def _resume(self, chat: Chat, flow: ChatFlow) -> bool:
        """Dev resumed a held chat: send the message if the field still holds it (Dev may have sent it himself: then
        it's ours anyway; anything else on our side → manual), or carry out the held unmatch."""
        held, now = chat.held, self.clock()
        chat.held = {}
        s = self.nav.stable()
        if not (s.kind is screens.Kind.CHAT and screens.chat_view(s.root).name == chat.name):
            self.nav.open_chat(chat.key)
        if held.get("kind") == "unmatch":
            self.nav.unmatch()
            flow.unmatched(chat, now)
            self.store.save_chat(chat)
            return True
        act = Action("send", template=held["template"], text=held["text"])
        if self.nav.field_holds(act.text):
            self.nav.send_message(act.text)
            flow.delivered(chat, act, now)
        else:
            msgs, _ = self.nav.read_chat()
            ours = [m for m in msgs if m.side == "right"]
            if ours and " ".join(ours[-1].text.split()) == " ".join(act.text.split()):
                flow.delivered(chat, act, now)   # Dev tapped send himself: the bot's message as typed
            else:
                chat.state, chat.reason, chat.closed_at = MANUAL, "Dev changed the held message", now
                chat.log.append(f"{MANUAL}: Dev changed the held message")
                self.notify(NOTICE[MANUAL])
        self.store.save_chat(chat)
        return True


class _Held(Exception):
    """A hold was raised: the pass stops, the loop waits for Dev."""


class _Limited(Exception):
    def __init__(self, until: float):
        super().__init__(until)
        self.until = until

