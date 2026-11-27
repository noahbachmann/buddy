"""Module config: a folder with `src/cad.toml`.

    <module>/            print plates (`cad plate`), README
    <module>/stl/        single parts (`cad build`)
    <module>/src/        cad.toml, .scad files (paths below are relative to it), renders/


    assembly = "assembly.scad"      # optional: posed preview, rendered with OpenSCAD colours

    [parts.lid]                     # one table per printable part; file defaults to <name>.scad
    [parts.couplers]
    file = "couplers.scad"
    defines = { n = 2 }             # optional -D overrides for this part

    [plates.1-petg]                 # optional: one print job = one STL in <module>/ (`cad plate`)
    note = "PETG · 0.2 mm"          # material and slicer settings, printed with the plate
    parts = ["a", "b", "b", ["c", "c-support"]]   # repeat = more copies; a list = one group in its own frame
    bed = [254, 254]                # optional: usable bed for this plate (default 230 × 230, room for brims)
    place = [[0, 0, 90], [95, 90, 0]]   # optional: fixed layout, one [x, y, turn°] per item (bbox min corner after
                                        # the turn) instead of packing: nests an L round another part
    gap = 6                         # optional: min mm between parts (default 8)
"""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

CONFIG = "cad.toml"
SRC = "src"
TOOLS_DIR = Path(__file__).resolve().parents[2]


def libpath() -> Path:
    return Path(os.environ.get("CAD_LIBPATH", TOOLS_DIR.parent / "lib")).resolve()


@dataclass
class Part:
    name: str
    file: Path
    defines: dict[str, object] = field(default_factory=dict)


@dataclass
class Plate:
    name: str
    note: str
    items: list[list[str]]       # each item: part names merged in their common coordinates
    bed: tuple[float, float] | None = None   # None = plate.BED
    place: list[tuple[float, float, float]] | None = None   # None = pack
    gap: float | None = None                 # None = plate.GAP


@dataclass
class Module:
    dir: Path
    parts: dict[str, Part]
    assembly: Path | None
    plates: dict[str, Plate] = field(default_factory=dict)

    @property
    def name(self) -> str:
        return self.dir.name

    @property
    def src_dir(self) -> Path:
        return self.dir / SRC

    @property
    def stl_dir(self) -> Path:
        return self.dir / "stl"

    @property
    def plate_dir(self) -> Path:
        return self.dir   # the plates are what Dev opens: the module root

    @property
    def render_dir(self) -> Path:
        return self.src_dir / "renders"

    def select(self, names: list[str]) -> list[Part]:
        unknown = [n for n in names if n not in self.parts]
        if unknown:
            raise SystemExit(f"{self.name}: unknown part(s) {', '.join(unknown)}; have {', '.join(self.parts)}")
        return [self.parts[n] for n in names] if names else list(self.parts.values())


def resolve(arg: str) -> Path:
    """CLI target: as given if it exists, else relative to the parts root (`cad build dispenser` from anywhere)."""
    p = Path(arg)
    if p.exists():
        return p
    q = TOOLS_DIR.parent / arg
    if q.exists():
        return q
    raise SystemExit(f"{arg}: not found (here or in {TOOLS_DIR.parent})")


def find_module(path: Path) -> Module | None:
    """The module that `path` (a folder or a file in it) belongs to, if any."""
    d = path.resolve()
    if d.is_file():
        d = d.parent
    for c in (d, *d.parents):   # the root, stl/, src/ or below (src/ref/)
        if c.name == SRC and (c / CONFIG).is_file():
            return load(c.parent)
        if (c / SRC / CONFIG).is_file():
            return load(c)
    return None


def load(d: Path) -> Module:
    d = d.resolve()
    src, cfg = d / SRC, d / SRC / CONFIG
    if not cfg.is_file():
        raise SystemExit(f"{d}: no {SRC}/{CONFIG}, not a module")
    with open(cfg, "rb") as f:
        raw = tomllib.load(f)
    parts = {}
    for name, spec in raw.get("parts", {}).items():
        file = src / spec.get("file", f"{name}.scad")
        if not file.is_file():
            raise SystemExit(f"{cfg}: part {name}: {file.name} missing")
        parts[name] = Part(name, file, dict(spec.get("defines", {})))
    plates = {}
    for name, spec in raw.get("plates", {}).items():
        items = [[i] if isinstance(i, str) else list(i) for i in spec.get("parts", [])]
        unknown = sorted({n for i in items for n in i} - parts.keys())
        if not items or unknown:
            raise SystemExit(f"{cfg}: plate {name}: " + (f"unknown part(s) {', '.join(unknown)}" if unknown else "no parts"))
        bed = spec.get("bed")
        if bed is not None and (len(bed) != 2 or not all(isinstance(v, (int, float)) and 0 < v <= 256 for v in bed)):
            raise SystemExit(f"{cfg}: plate {name}: bed must be [w, d] in mm, at most 256 (X2D)")
        place = spec.get("place")
        if place is not None and (len(place) != len(items) or not all(
                len(q) == 3 and all(isinstance(v, (int, float)) for v in q) for q in place)):
            raise SystemExit(f"{cfg}: plate {name}: place needs one [x, y, turn] per item")
        gap = spec.get("gap")
        if gap is not None and not (isinstance(gap, (int, float)) and gap >= 0):
            raise SystemExit(f"{cfg}: plate {name}: gap must be a number ≥ 0")
        plates[name] = Plate(name, spec.get("note", ""), items, tuple(float(v) for v in bed) if bed else None,
                             [tuple(float(v) for v in q) for q in place] if place else None,
                             float(gap) if gap is not None else None)
    asm = raw.get("assembly")
    return Module(d, parts, src / asm if asm else None, plates)
