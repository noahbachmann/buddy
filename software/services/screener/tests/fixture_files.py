"""The sanitized dumps in fixtures/ (step 1, 2026-11-16): one profile walked top to bottom, the comment sheet,
the lists, a chat, a promo popup and a detail row walked sideways (2026-11-17), and screens the model must not recognise."""

from pathlib import Path

FIXTURES = Path(__file__).parent / "fixtures"
PACKAGE = "app.example"
FEED = [f"feed-{i:02}" for i in range(1, 12)]   # one profile, top to bottom, ~600–900 px per step
ROW = [f"row-{i}" for i in range(4)]   # another profile's detail row swiped sideways to its end (2026-11-17)


def load(name: str) -> str:
    return (FIXTURES / f"{name}.xml").read_text()
