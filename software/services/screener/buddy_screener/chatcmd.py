"""Dev's chat commands (#88): review the chats from afar (`chats`), replace a held message with Dev's wording
(`chat-edit`), hand a `manual` chat back to the bot (`chat-back`). Names and texts are printed here (Dev's own
terminal), never on Telegram or in the journal."""

import argparse
import json
import sys
import time

from .chat import CLOSED, HANDOVER, MANUAL, OPEN, Chat
from .chats import HOLD_CHAT_MESSAGE, note_path
from .config import Config
from .store import Store

CHAT_TASKS = ("converse", "verify")


def find(store: Store, name: str, states: tuple[str, ...]) -> Chat | None:
    """The one chat in `states` with this name (or key); none or several → None, with a message."""
    hits = [c for c in store.chats(states) if name in (c.name, c.key)]
    if len(hits) != 1:
        print(f"{len(hits)} chats named {name!r} in {'/'.join(states)}", file=sys.stderr)
        return None
    return hits[0]


def _call_line(c: dict) -> str:
    when = time.strftime("%m-%d %H:%M", time.localtime(c["ts"]))
    tokens = c["in"] + c["cache_read"] + c["cache_write"]
    return (f"  {when} {c['task']:8} in {tokens:>5} (cache read {c['cache_read']}, written {c['cache_write']}) "
            f"out {c['out']:>4}  ${c['usd']:.4f}")


def chats(cfg: Config, store: Store) -> list[str]:
    out = []
    for c in store.chats(OPEN + (MANUAL, HANDOVER, CLOSED)):
        if c.state == CLOSED and (c.unmatched or c.unmatch_at is None):
            continue
        out.append(f"=== {c.name}: {c.state}{' (held)' if c.held else ''}, {len(c.calls)} calls ${c.cost:.3f}")
        out += [_call_line(x) for x in c.calls[-3:]]
        note = note_path(cfg.tmp_dir / "chats", c.key)
        out += note.read_text().splitlines()[1:] if note.exists() else ["(not read since the last restart)"]
        out.append("")
    rows = [r for r in store.usage_rows(time.time() - 86400) if r[0] in CHAT_TASKS]
    out.append(f"chat calls, last 24 h: {len(rows)}, ${sum(r[6] for r in rows):.3f} API-equivalent "
               f"(the plan's 5 h window was at {rows[-1][8] or 0:.0%} at the last one, shared with Claude Code)"
               if rows else "chat calls, last 24 h: none")
    return out


def chat_edit(cfg: Config, store: Store, name: str, text: str) -> int:
    """Dev's wording for the held message: clear the field, type his text, read it back; Resume sends it."""
    from .main import _dev_navigator, _device
    from .screens import Kind, chat_view

    try:
        held = json.loads(cfg.needs_you_file.read_text())
    except (OSError, ValueError):
        held = {}
    if held.get("kind") != HOLD_CHAT_MESSAGE:
        print("the loop isn't holding a chat message", file=sys.stderr)
        return 2
    if (chat := find(store, name, OPEN)) is None or chat.held.get("kind") != "send":
        print(f"no held message in {name!r}", file=sys.stderr)
        return 2
    nav = _dev_navigator(cfg, _device(cfg))
    s = nav.expect(Kind.CHAT)
    if chat_view(s.root).name != chat.name:
        print(f"the phone shows another chat ({chat_view(s.root).name!r})", file=sys.stderr)
        return 3
    nav.clear_message()
    nav.type_message(text)
    chat.held = chat.held | {"text": text, "dev": "1"}
    chat.log.append("Dev's wording for the held message")
    store.save_chat(chat)
    note = note_path(cfg.tmp_dir / "chats", chat.key)
    if note.exists():
        lines = [x for x in note.read_text().splitlines() if not x.startswith("HELD (")]
        note.write_text("\n".join(lines + [f"HELD ({chat.held['template']}, Dev): {text}"]) + "\n")
    print(f"{chat.name}: typed and read back; Resume sends it")
    return 0


def run(cfg: Config, args: argparse.Namespace) -> int:
    from .hub import queue_give_back

    store = Store(cfg.db_path)
    if args.cmd == "chats":
        print("\n".join(chats(cfg, store)))
        return 0
    if args.cmd == "chat-edit":
        if len(args.args) != 2 or not args.args[1].strip():
            print('chat-edit <name> "<text>"', file=sys.stderr)
            return 2
        return chat_edit(cfg, store, args.args[0], args.args[1].strip())
    if args.cmd == "chat-back":
        if len(args.args) != 1:
            print("chat-back <name>", file=sys.stderr)
            return 2
        if (chat := find(store, args.args[0], (MANUAL, HANDOVER))) is None:
            return 2
        queue_give_back(cfg, chat.key)
        print(f"{chat.name}: queued; the loop's next step hands it back (after a Resume if it's held)")
        return 0
    print(f"unknown --cmd {args.cmd}", file=sys.stderr)
    return 2
