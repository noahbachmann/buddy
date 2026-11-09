"""Module config: a folder with `cad.toml`.

    assembly = "assembly.scad"      # optional: posed preview, rendered with OpenSCAD colours

    [parts.upper-arm-top]           # one table per printable part; file defaults to <name>.scad
    [parts.couplers]
    file = "couplers.scad"
    defines = { n = 2 }             # optional -D overrides for this part

    [plates.1-petg]                 # optional: one print job = one STL in <module>/plates (`cad plate`)
    note = "PETG · 0.2 mm"          # material and slicer settings, printed with the plate
    parts = ["a", "b", "b", ["c", "c-support"]]   # repeat = more copies; a list = one group in its own frame
"""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

CONFIG = "cad.toml"
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
    def stl_dir(self) -> Path:
        return self.dir / "stl"

    @property
    def plate_dir(self) -> Path:
        return self.dir / "plates"

    @property
    def render_dir(self) -> Path:
        return self.dir / "renders"

    def select(self, names: list[str]) -> list[Part]:
        unknown = [n for n in names if n not in self.parts]
        if unknown:
            raise SystemExit(f"{self.name}: unknown part(s) {', '.join(unknown)}; have {', '.join(self.parts)}")
        return [self.parts[n] for n in names] if names else list(self.parts.values())


def resolve(arg: str) -> Path:
    """CLI target: as given if it exists, else relative to the parts root (`cad build arm` from anywhere)."""
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
    return load(d) if (d / CONFIG).is_file() else None


def load(d: Path) -> Module:
    d = d.resolve()
    if not (d / CONFIG).is_file():
        raise SystemExit(f"{d}: no {CONFIG}, not a module")
    with open(d / CONFIG, "rb") as f:
        raw = tomllib.load(f)
    parts = {}
    for name, spec in raw.get("parts", {}).items():
        file = d / spec.get("file", f"{name}.scad")
        if not file.is_file():
            raise SystemExit(f"{d / CONFIG}: part {name}: {file.name} missing")
        parts[name] = Part(name, file, dict(spec.get("defines", {})))
    plates = {}
    for name, spec in raw.get("plates", {}).items():
        items = [[i] if isinstance(i, str) else list(i) for i in spec.get("parts", [])]
        unknown = sorted({n for i in items for n in i} - parts.keys())
        if not items or unknown:
            raise SystemExit(f"{d / CONFIG}: plate {name}: " + (f"unknown part(s) {', '.join(unknown)}" if unknown else "no parts"))
        plates[name] = Plate(name, spec.get("note", ""), items)
    asm = raw.get("assembly")
    return Module(d, parts, d / asm if asm else None, plates)
