"""Thin OpenSCAD wrapper: always Manifold, strict about warnings."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from .config import libpath

OPENSCAD = os.environ.get("CAD_OPENSCAD", "openscad")


class ScadError(Exception):
    pass


def literal(v: object) -> str:
    """Python value → OpenSCAD literal for -D."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, (list, tuple)):
        return "[" + ",".join(literal(x) for x in v) + "]"
    return repr(v)


def parse_define(s: str) -> tuple[str, object]:
    """`name=value` from the CLI; value is kept verbatim (already OpenSCAD syntax)."""
    name, sep, value = s.partition("=")
    if not sep or not name:
        raise SystemExit(f"bad define {s!r}, want name=value")
    return name.strip(), _Raw(value.strip())


class _Raw(str):
    pass


def run(src: Path, out: Path, defines: dict[str, object] | None = None, extra: list[str] | None = None) -> str:
    """Runs OpenSCAD; raises ScadError on a non-zero exit or any WARNING/ERROR line. Returns the log."""
    args = [OPENSCAD, "--backend=manifold", "-o", str(out)]
    for k, v in (defines or {}).items():
        args += ["-D", f"{k}={v if isinstance(v, _Raw) else literal(v)}"]
    args += (extra or []) + [str(src)]
    env = {**os.environ, "OPENSCADPATH": str(libpath())}
    p = subprocess.run(args, capture_output=True, text=True, env=env)
    log = p.stdout + p.stderr
    bad = [ln for ln in log.splitlines() if ln.startswith(("WARNING", "ERROR", "DEPRECATED"))]
    if p.returncode or bad:
        raise ScadError(f"{src.name}: " + ("\n  ".join(bad) if bad else log.strip()))
    return log


def export_stl(src: Path, out: Path, defines: dict[str, object] | None = None) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    run(src, out, defines, ["--export-format=binstl"])


def export_png(src: Path, out: Path, rot: tuple[float, float, float], size: tuple[int, int],
               defines: dict[str, object] | None = None) -> None:
    """Preview render (keeps colour() and % ghosts), orthographic, framed to fit.
    `rot` = OpenSCAD gimbal camera rotation: (0,0,0) looks down from +Z with +Y up, (90,0,0) from −Y."""
    out.parent.mkdir(parents=True, exist_ok=True)
    run(src, out, defines, [
        f"--camera=0,0,0,{rot[0]},{rot[1]},{rot[2]},500", "--viewall", "--autocenter",
        "--projection=ortho", "--view=axes,scales", f"--imgsize={size[0]},{size[1]}",
        "--colorscheme=Tomorrow",
    ])
