"""tools/voice/commands.tsv: sentence → set, intent, query."""

from dataclasses import dataclass
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "commands.tsv"


@dataclass(frozen=True)
class Command:
    set: str  # train | test | wake
    sentence: str
    intent: str
    query: str


def load(path: Path = PATH) -> list[Command]:
    out = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 2 or cols[0] not in ("train", "test", "wake"):
            raise ValueError(f"{path}:{n}: expected <train|test|wake> TAB <sentence> TAB <intent> [TAB <query>]")
        cols += [""] * (4 - len(cols))
        out.append(Command(*cols[:4]))
    return out


def by_sentence(commands: list[Command]) -> dict[str, Command]:
    return {c.sentence: c for c in commands}
