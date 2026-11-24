"""The service's side of the loop (#80): Dev's holds on Telegram, the answers, the retained state. No I/O but files.

A hold writes `needs_you` (the loop and its input stop) and asks Dev through `messenger` with Resume/Stop buttons.
Texts are fixed per kind: never a reason string, a name or anything from a profile (bot chats aren't end-to-end
encrypted, D22). `resume` (Telegram, panel, MQTT) removes the file and withdraws an open question.
"""

import json
import time
from collections.abc import Callable
from typing import Any

from buddy_shared import topics

from .browse import HOLD_DECIDE, HOLD_FILES, HOLD_LIKES, HOLD_SCREEN, HOLD_WOULD_LIKE
from .chats import HOLD_CHAT_FILES, HOLD_CHAT_MESSAGE, HOLD_CHAT_SCREEN, HOLD_CHAT_UNMATCH
from .config import Config

OPTIONS = ["Resume", "Stop"]
# What happened and what Dev does about it. {why}: the code's reason (the navigator's carry no names, D22 allows it).
HOLD_TEXT = {
    HOLD_WOULD_LIKE: "Screener would like a profile (row #{row}). It's on the phone now.\n"
                     "To do: like it on the phone (or tell Claude \"send #{row}\"), or skip it if you don't want it. "
                     "Then Resume.",
    HOLD_SCREEN: "Screener stopped at {time}: {why}.\n"
                 "To do: look at the phone. If it isn't on a profile in Discover (a popup, a dialog, another app, the "
                 "screen off), bring it back there, then Resume. If it is, or it stops at the same spot again, tell "
                 "Claude: the screen is saved on the Pi.",
    HOLD_LIKES: "Screener stopped: {why}. The likes didn't come back when expected (24 h after the last like).\n"
                "To do: nothing on the phone. Tell Claude (the reset rule needs a look). Resume tries again.",
    HOLD_DECIDE: "Screener stopped at {time}: its decisions failed 3 times in a row ({why}).\n"
                 "To do: usually the plan's usage or the network. Resume tries again; if it keeps failing, tell Claude.",
    HOLD_FILES: "Screener stopped: preferences.md or templates.md was refused ({why}).\n"
                "To do: fix that line (ssh -t buddy micro ~/buddy/config/screener/templates.md), then Resume.",
    # chats (#77): {why} is the message's kind (disclosure, reply, nudge, …) or the navigator's reason, never a text
    HOLD_CHAT_MESSAGE: "Screener typed a chat message ({why}) and didn't send it. It's in the open chat on the phone.\n"
                       "To do: read it there. Fine → Resume (the bot sends it). Not fine → write your own instead "
                       "(that chat is yours from then on), then Resume.",
    HOLD_CHAT_UNMATCH: "Screener wants to unmatch a closed chat (no fit, or she opted out, a day ago).\n"
                       "To do: Resume lets it unmatch. To keep the match, tell Claude first.",
    HOLD_CHAT_SCREEN: "Screener stopped in the chats at {time}: {why}.\n"
                      "To do: look at the phone. Bring it back to the matches list (no dialog, no menu open), then "
                      "Resume. If it stops at the same spot again, tell Claude: the screen is saved on the Pi.",
    HOLD_CHAT_FILES: "Screener's chats stopped: chat.md, templates.md or a contact file was refused ({why}).\n"
                     "To do: fix it (ssh -t buddy micro ~/buddy/config/screener/chat.md), then Resume.",
}
WHY_MAX = 200


class Hub:
    def __init__(self, cfg: Config, publish: Callable[[str, dict], None], clock: Callable[[], float] = time.time):
        self.cfg, self.publish, self.clock = cfg, publish, clock

    def hold(self, kind: str, row_id: int | None, why: str = "") -> None:
        ask = f"screener-{int(self.clock() * 1000)}"
        self.cfg.data_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.cfg.needs_you_file.write_text(json.dumps({"kind": kind, "row": row_id, "ask": ask}))
        text = HOLD_TEXT[kind].format(row=row_id, why=" ".join(why.split())[:WHY_MAX] or "no reason given",
                                      time=time.strftime("%H:%M", time.localtime(self.clock())))
        self.publish(topics.messenger_cmd, {"action": "ask", "id": ask, "from": "screener", "ttl_s": 0,
                                            "text": text, "options": OPTIONS})

    def notify(self, text: str) -> None:
        """Info only (no sound): limits, likes used up. `text` is the loop's, built from counts and times."""
        self.publish(topics.messenger_cmd, {"action": "notify", "level": "info", "text": f"Screener: {text}"})

    def held(self) -> dict[str, Any] | None:
        try:
            return json.loads(self.cfg.needs_you_file.read_text())
        except (OSError, ValueError):
            return None

    def answer(self, payload: dict[str, Any]) -> str | None:
        """A messenger event → `resume` / `stop` when it answers the open hold, else None."""
        h = self.held()
        if not h or payload.get("event") != "answered" or payload.get("id") != h.get("ask"):
            return None
        return {0: "resume", 1: "stop"}.get(payload.get("option"))

    def resumed(self, how: str = "on the panel") -> None:
        """The hold is over: Dev's question on Telegram says so instead of "withdrawn" (an answer there needs no
        note: messenger already shows it)."""
        h = self.held()
        self.cfg.needs_you_file.unlink(missing_ok=True)
        if h and h.get("ask"):
            self.publish(topics.messenger_cmd, {"action": "cancel", "id": h["ask"], "note": f"▶ resumed {how}"})
