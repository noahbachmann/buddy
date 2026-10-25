"""All MQTT topic names, loaded from shared/topics.json (the single source; C++ gets a generated topics.hpp).

    topics.ears_state == "buddy/ears/state"
    topics.status("ears") == "buddy/ears/status"
"""

import json
from pathlib import Path

_here = Path(__file__).resolve().parent
# Installed wheel: next to this file. Editable install / source tree: shared/topics.json.
_file = next(p for p in (_here / "topics.json", _here.parents[1] / "topics.json") if p.exists())

SPEC: list[dict] = json.loads(_file.read_text())["topics"]
globals().update({t["name"]: t["topic"] for t in SPEC})


def status(service: str) -> str:
    return f"buddy/{service}/status"


def matches(filter: str, topic: str) -> bool:
    """MQTT wildcard match: matches("buddy/+/status", "buddy/ears/status") is True."""
    f, t = filter.split("/"), topic.split("/")
    for i, part in enumerate(f):
        if part == "#":
            return True
        if i >= len(t) or (part != "+" and part != t[i]):
            return False
    return len(f) == len(t)


def __getattr__(name: str) -> str:  # typo → clear error instead of a silent None
    raise AttributeError(f"no topic {name!r} in {_file}")
