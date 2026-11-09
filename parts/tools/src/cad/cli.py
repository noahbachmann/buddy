"""`cad` command line. Run via the `parts/cad` wrapper.

    cad build <module> [part…] [-D name=value]         STLs into <module>/stl, strict (warnings fail)
    cad plate <module> [plate…]                        print plates ([plates] in cad.toml) into <module>/plates
    cad render <module|file.scad|file.stl> [part…]     PNG sheets into <dir>/renders, paths printed
        --section z=40 --section x=0                   2D cuts per part
        -D elbow=60                                    OpenSCAD defines (poses, variants)
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import trimesh

from . import config, mesh, scad


def _build(parts: list[config.Part], out_dir: Path, extra: dict[str, object]) -> dict[str, Path]:
    def one(p: config.Part) -> tuple[str, Path]:
        out = out_dir / f"{p.name}.stl"
        scad.export_stl(p.file, out, {**p.defines, **extra})
        return p.name, out

    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        return dict(ex.map(one, parts))


def cmd_build(a: argparse.Namespace) -> int:
    mod = config.load(config.resolve(a.module))
    parts = mod.select(a.parts)
    try:
        built = _build(parts, mod.stl_dir, dict(scad.parse_define(d) for d in a.define))
    except scad.ScadError as e:
        print(f"FAIL {e}", file=sys.stderr)
        return 1
    bad = 0
    for name, path in built.items():
        st = mesh.stats(mesh.load(path))
        bad += not st.watertight
        print(f"{name:20} {st.line()}")
    return 1 if bad else 0


def cmd_plate(a: argparse.Namespace) -> int:
    from . import plate

    mod = config.load(config.resolve(a.module))
    unknown = [n for n in a.plates if n not in mod.plates]
    if not mod.plates or unknown:
        raise SystemExit(f"{mod.name}: " + (f"unknown plate(s) {', '.join(unknown)}; have {', '.join(mod.plates)}"
                                            if unknown else f"no [plates] in {config.CONFIG}"))
    plates = [mod.plates[n] for n in a.plates] if a.plates else list(mod.plates.values())
    names = sorted({n for p in plates for i in p.items for n in i})
    mod.plate_dir.mkdir(exist_ok=True)
    if not a.plates:   # full run: drop files of plates that no longer exist
        for old in mod.plate_dir.glob("*.stl"):
            if old.stem not in mod.plates:
                old.unlink()
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        try:
            built = _build(mod.select(names), Path(tmp), dict(scad.parse_define(d) for d in a.define))
        except scad.ScadError as e:
            print(f"FAIL {e}", file=sys.stderr)
            return 1
        meshes = {n: mesh.load(p) for n, p in built.items()}
        for p in plates:
            items = [trimesh.util.concatenate([meshes[n] for n in i]) for i in p.items]
            try:
                m = plate.pack(items)
            except ValueError as e:
                print(f"FAIL {p.name}: {e}", file=sys.stderr)
                return 1
            out = mod.plate_dir / f"{p.name}.stl"
            m.export(out)
            st = mesh.stats(m)
            bad += not st.watertight
            sx, sy, sz = st.size
            print(f"{p.name}: {p.note}\n  {_count(p.items)}\n  {sx:.0f} × {sy:.0f} × {sz:.0f} mm · "
                  f"{st.volume_cm3:.1f} cm³ solid" + ("" if st.watertight else " · NOT WATERTIGHT") + f"\n  {out}")
    return 1 if bad else 0


def _count(items: list[list[str]]) -> str:
    seen: dict[str, int] = {}
    for i in items:
        k = " + ".join(i)
        seen[k] = seen.get(k, 0) + 1
    return ", ".join(f"{n}× {k}" if n > 1 else k for k, n in seen.items())


def _sections(specs: list[str]) -> list[tuple[str, float]]:
    out = []
    for s in specs:
        axis, sep, val = s.partition("=")
        if not sep or axis.lower() not in "xyz" or len(axis) != 1:
            raise SystemExit(f"bad --section {s!r}, want x|y|z=<mm>")
        out.append((axis.lower(), float(val)))
    return out


def _render_mesh(m, title: str, stem: str, out_dir: Path, sections: list[tuple[str, float]]) -> list[Path]:
    from . import render

    paths = [render.part_sheet(m, title, out_dir / f"{stem}.png")]
    for axis, v in sections:
        paths.append(render.section(m, axis, v, title, out_dir / f"{stem}-{axis}{v:g}.png"))
    return paths


def cmd_render(a: argparse.Namespace) -> int:
    from . import render

    target = config.resolve(a.target)
    defines = dict(scad.parse_define(d) for d in a.define)
    sections = _sections(a.section)
    mod = config.find_module(target)
    out_dir = Path(a.out) if a.out else (mod.render_dir if mod else target.resolve().parent / "renders")
    written: list[Path] = []
    try:
        if target.is_dir():
            if mod is None:
                raise SystemExit(f"{target}: no {config.CONFIG}")
            with tempfile.TemporaryDirectory() as tmp:
                built = _build(mod.select(a.parts), Path(tmp), defines)
                for name, stl in built.items():
                    written += _render_mesh(mesh.load(stl), f"{mod.name}/{name}", name, out_dir, sections)
            if not a.parts and mod.assembly:
                written.append(render.assembly_sheet(mod.assembly, defines, f"{mod.name}/assembly",
                                                     out_dir / "assembly.png"))
        elif target.suffix == ".stl":
            written += _render_mesh(mesh.load(target), target.name, target.stem, out_dir, sections)
        elif target.suffix == ".scad":
            if mod and mod.assembly and mod.assembly.resolve() == target.resolve():
                written.append(render.assembly_sheet(target, defines, f"{mod.name}/{target.stem}",
                                                     out_dir / f"{target.stem}.png"))
            else:
                with tempfile.TemporaryDirectory() as tmp:
                    stl = Path(tmp) / f"{target.stem}.stl"
                    scad.export_stl(target, stl, defines)
                    written += _render_mesh(mesh.load(stl), target.name, target.stem, out_dir, sections)
        else:
            raise SystemExit(f"{target}: want a module folder, .scad or .stl")
    except scad.ScadError as e:
        print(f"FAIL {e}", file=sys.stderr)
        return 1
    for p in written:
        print(p)
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="cad", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="render STLs of a module")
    b.add_argument("module")
    b.add_argument("parts", nargs="*")
    b.add_argument("-D", dest="define", action="append", default=[], metavar="NAME=VALUE")
    b.set_defaults(fn=cmd_build)

    p = sub.add_parser("plate", help="print plates: parts packed per print job, one STL each")
    p.add_argument("module")
    p.add_argument("plates", nargs="*")
    p.add_argument("-D", dest="define", action="append", default=[], metavar="NAME=VALUE")
    p.set_defaults(fn=cmd_plate)

    r = sub.add_parser("render", help="PNG contact sheets, sections, assembly views")
    r.add_argument("target")
    r.add_argument("parts", nargs="*")
    r.add_argument("--section", action="append", default=[], metavar="AXIS=MM")
    r.add_argument("-D", dest="define", action="append", default=[], metavar="NAME=VALUE")
    r.add_argument("-o", "--out", help="output folder (default: <module>/renders)")
    r.set_defaults(fn=cmd_render)

    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
