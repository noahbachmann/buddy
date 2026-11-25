"""Dev's chat commands (#88): hand a `manual` chat back to the bot. Names are printed here (Dev's own terminal),
never on Telegram or in the journal."""

import argparse
import sys

from .chat import MANUAL, Chat
from .config import Config
from .store import Store


def find(store: Store, name: str, states: tuple[str, ...]) -> Chat | None:
    """The one chat in `states` with this name (or key); none or several → None, with a message."""
    hits = [c for c in store.chats(states) if name in (c.name, c.key)]
    if len(hits) != 1:
        print(f"{len(hits)} chats named {name!r} in {'/'.join(states)}", file=sys.stderr)
        return None
    return hits[0]


def run(cfg: Config, args: argparse.Namespace) -> int:
    from .hub import queue_give_back

    store = Store(cfg.db_path)
    if args.cmd == "chat-back":
        if len(args.args) != 1:
            print("chat-back <name>", file=sys.stderr)
            return 2
        if (chat := find(store, args.args[0], (MANUAL,))) is None:
            return 2
        queue_give_back(cfg, chat.key)
        print(f"{chat.name}: queued; the loop's next step hands it back (after a Resume if it's held)")
        return 0
    print(f"unknown --cmd {args.cmd}", file=sys.stderr)
    return 2
