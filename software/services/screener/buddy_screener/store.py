"""SQLite state in `~/buddy/data/screener/screener.db` (host only).

- `usage`: one row per Claude call (kept: it sets active hours and model choice).
- `shadow`: step 2's labelled set: the bot's decision on a profile and Dev's choice for it, and where the capture's
  time went (`capture`, #89). No name, no images, no profile text: a profile is a key (hash of name, age, first
  prompt). Rows older than `KEEP_DAYS` are deleted.
- `actions` (#80): every act on the phone and every stop of the loop (kind, run mode, shadow row id, a detail that is
  never a name or profile text): the like budget counts from it. `KEEP_DAYS`.
- `chats` (#77): one row per chat, `chat.Chat` as JSON (state, answers, flags, our sent log with its times; never her
  message texts). Deleted `KEEP_DAYS` after the chat ended (closed, done, handed over or manual).
"""

import hashlib
import json
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .chat import LIKED, OPEN, Chat

KEEP_DAYS = 30

_SCHEMA = """
CREATE TABLE IF NOT EXISTS usage (
    ts REAL NOT NULL, task TEXT NOT NULL, model TEXT NOT NULL,
    input INTEGER, cache_read INTEGER, cache_write INTEGER, output INTEGER,
    cost REAL, ms INTEGER, five_hour REAL, seven_day REAL
);
CREATE TABLE IF NOT EXISTS shadow (
    id INTEGER PRIMARY KEY, ts REAL NOT NULL, key TEXT NOT NULL,
    capture_s REAL, decide_ms INTEGER, cost REAL, tokens INTEGER,
    bot TEXT NOT NULL,           -- decide.Browse as JSON
    dev TEXT, labelled REAL,     -- Dev's label as JSON
    capture TEXT                 -- navigator.CaptureStats as JSON (#89)
);
CREATE TABLE IF NOT EXISTS actions (
    id INTEGER PRIMARY KEY, ts REAL NOT NULL, kind TEXT NOT NULL,
    mode TEXT NOT NULL,          -- dry | live | shadow (Dev's label carried out)
    row_id INTEGER, detail TEXT  -- detail: counts, reasons by kind; never a name or profile text
);
CREATE TABLE IF NOT EXISTS chats (
    key TEXT PRIMARY KEY, state TEXT NOT NULL, started REAL NOT NULL, updated REAL NOT NULL,
    ended REAL,                  -- closed / done / handover / manual: retention counts from here
    data TEXT NOT NULL           -- chat.Chat as JSON
);
"""


def profile_key(name: str, details: list[tuple[str, str]], first_prompt: str) -> str:
    age = next((v for label, v in details if label == "Age"), "")
    return hashlib.sha256(f"{name}\0{age}\0{first_prompt}".encode()).hexdigest()[:16]


@dataclass
class Action:
    ts: float
    kind: str
    mode: str
    row_id: int | None = None
    detail: str = ""


@dataclass
class ShadowRow:
    id: int
    ts: float
    key: str
    capture_s: float
    decide_ms: int
    cost: float
    tokens: int
    bot: dict[str, Any]
    dev: dict[str, Any] | None
    capture: dict[str, Any] | None = None


class Store:
    def __init__(self, path: Path | str, clock=time.time):
        if isinstance(path, Path):
            path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.db = sqlite3.connect(str(path))
        # WAL: fewer syncs to the microSD per commit, and a `--cmd` reading while the service writes doesn't block
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self._seen: dict[str, tuple[str, str]] = {}   # chat key → (state, data) as last read or written here
        self.db.executescript(_SCHEMA)
        if "capture" not in {r[1] for r in self.db.execute("PRAGMA table_info(shadow)")}:
            self.db.execute("ALTER TABLE shadow ADD COLUMN capture TEXT")   # a DB from before #89
        self.clock = clock
        self.db.execute("DELETE FROM shadow WHERE ts < ?", (clock() - KEEP_DAYS * 86400,))
        self.db.execute("DELETE FROM actions WHERE ts < ?", (clock() - KEEP_DAYS * 86400,))
        self.db.execute("DELETE FROM chats WHERE ended IS NOT NULL AND ended < ?", (clock() - KEEP_DAYS * 86400,))
        self.db.commit()

    def add_usage(self, ts: float, u) -> None:
        self.db.execute("INSERT INTO usage VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                        (ts, u.task, u.model, u.input, u.cache_read, u.cache_write, u.output, u.cost, u.ms,
                         u.five_hour, u.seven_day))
        self.db.commit()

    def cost_since(self, ts: float, sim: bool = False, chat: bool | None = None) -> float:
        """$ since `ts`: the live calls, or (`sim`) only the sim's: each has its own cap, neither eats the other's.
        `chat` (live only, #88): True = only the chat calls, False = all but them."""
        op = "LIKE" if sim else "NOT LIKE"
        where = f"ts >= ? AND task {op} 'sim-%'"
        if not sim and chat is not None:
            where += f" AND task {'IN' if chat else 'NOT IN'} ('converse', 'verify')"
        return self.db.execute(f"SELECT COALESCE(SUM(cost), 0) FROM usage WHERE {where}", (ts,)).fetchone()[0]

    def add_shadow(self, key: str, capture_s: float, bot: dict[str, Any], usage,
                   capture: dict[str, Any] | None = None) -> int:
        tokens = usage.input + usage.cache_read + usage.cache_write + usage.output
        cur = self.db.execute(
            "INSERT INTO shadow (ts, key, capture_s, decide_ms, cost, tokens, bot, capture) VALUES (?,?,?,?,?,?,?,?)",
            (self.clock(), key, capture_s, usage.ms, usage.cost, tokens, json.dumps(bot, ensure_ascii=False),
             None if capture is None else json.dumps(capture)))
        self.db.commit()
        return cur.lastrowid

    def label(self, row_id: int, dev: dict[str, Any]) -> None:
        cur = self.db.execute("UPDATE shadow SET dev = ?, labelled = ? WHERE id = ?",
                              (json.dumps(dev, ensure_ascii=False), self.clock(), row_id))
        self.db.commit()
        if cur.rowcount != 1:
            raise KeyError(f"no shadow row {row_id}")

    def shadow(self, row_id: int | None = None) -> list[ShadowRow]:
        q = "SELECT id, ts, key, capture_s, decide_ms, cost, tokens, bot, dev, capture FROM shadow"
        rows = self.db.execute(q + (" WHERE id = ?" if row_id is not None else " ORDER BY id"),
                               (() if row_id is None else (row_id,))).fetchall()
        return [ShadowRow(r[0], r[1], r[2], r[3] or 0.0, r[4] or 0, r[5] or 0.0, r[6] or 0, json.loads(r[7]),
                          json.loads(r[8]) if r[8] else None, json.loads(r[9]) if r[9] else None) for r in rows]

    def add_action(self, kind: str, mode: str, row_id: int | None = None, detail: str = "",
                   ts: float | None = None) -> None:
        self.db.execute("INSERT INTO actions (ts, kind, mode, row_id, detail) VALUES (?,?,?,?,?)",
                        (self.clock() if ts is None else ts, kind, mode, row_id, detail))
        self.db.commit()

    def actions(self, since: float = 0.0) -> list[Action]:
        rows = self.db.execute("SELECT ts, kind, mode, row_id, detail FROM actions WHERE ts >= ? ORDER BY ts, id",
                               (since,)).fetchall()
        return [Action(r[0], r[1], r[2], r[3], r[4] or "") for r in rows]

    def usage_rows(self, since: float = 0.0, until: float = float("inf")) -> list[tuple]:
        return self.db.execute("SELECT task, model, input, cache_read, cache_write, output, cost, ms, five_hour, "
                               "seven_day FROM usage WHERE ts >= ? AND ts <= ? ORDER BY ts", (since, until)).fetchall()

    def save_chat(self, chat: Chat) -> None:
        """Writes only a change against the row as this store last saw it (read or written): the driver saves at
        every step, mostly unchanged. `updated` = the last change."""
        data = json.dumps(chat.to_dict(), ensure_ascii=False)
        if self._seen.get(chat.key) == (chat.state, data):
            return
        ended = None if chat.state in OPEN else (chat.closed_at or self.clock())
        self.db.execute("INSERT INTO chats (key, state, started, updated, ended, data) VALUES (?,?,?,?,?,?) "
                        "ON CONFLICT(key) DO UPDATE SET state=excluded.state, updated=excluded.updated, "
                        "ended=excluded.ended, data=excluded.data",
                        (chat.key, chat.state, chat.started, self.clock(), ended, data))
        self.db.commit()
        self._seen[chat.key] = (chat.state, data)

    def chat(self, key: str) -> Chat | None:
        row = self.db.execute("SELECT state, data FROM chats WHERE key = ?", (key,)).fetchone()
        if row is None:
            return None
        self._seen[key] = (row[0], row[1])
        return Chat.from_dict(json.loads(row[1]))

    def drop_chat(self, key: str) -> None:
        self.db.execute("DELETE FROM chats WHERE key = ?", (key,))
        self.db.commit()
        self._seen.pop(key, None)

    def liked(self, name: str) -> list[Chat]:
        """Our likes with this name still waiting for their match (#77)."""
        return [c for c in self.chats((LIKED,)) if c.name == name]

    def chats(self, states: tuple[str, ...] | None = None) -> list[Chat]:
        rows = self.db.execute("SELECT key, state, data FROM chats ORDER BY started").fetchall()
        out = []
        for key, st, d in rows:
            self._seen[key] = (st, d)
            if states is None or st in states:
                out.append(Chat.from_dict(json.loads(d)))
        return out
