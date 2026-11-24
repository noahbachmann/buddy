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
from .config import Config

OPTIONS = ["Resume", "Stop"]
HOLD_TEXT = {
    HOLD_WOULD_LIKE: "Screener: a profile it would like waits on the phone (row #{row}). Like it or skip it, "
                     "then resume.",
    HOLD_SCREEN: "Screener stopped at a screen it couldn't handle. It waits on the phone until you resume.",
    HOLD_LIKES: "Screener stopped: the likes didn't come back as expected (paywall). It waits until you resume.",
    HOLD_DECIDE: "Screener stopped: its decisions keep failing. It waits until you resume.",
    HOLD_FILES: "Screener stopped: preferences.md or templates.md was refused (a broken line?). Fix it, then resume.",
}


class Hub:
    def __init__(self, cfg: Config, publish: Callable[[str, dict], None], clock: Callable[[], float] = time.time):
        self.cfg, self.publish, self.clock = cfg, publish, clock

    def hold(self, kind: str, row_id: int | None) -> None:
        ask = f"screener-{int(self.clock() * 1000)}"
        self.cfg.data_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.cfg.needs_you_file.write_text(json.dumps({"kind": kind, "row": row_id, "ask": ask}))
        self.publish(topics.messenger_cmd, {"action": "ask", "id": ask, "from": "screener", "ttl_s": 0,
                                            "text": HOLD_TEXT[kind].format(row=row_id), "options": OPTIONS})

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

    def resumed(self) -> None:
        h = self.held()
        self.cfg.needs_you_file.unlink(missing_ok=True)
        if h and h.get("ask"):
            self.publish(topics.messenger_cmd, {"action": "cancel", "id": h["ask"]})
