"""Dev's dispenser.stl + roller + pusher -> dispenser-roller.stl (dispenser.stl stays untouched).

    cd parts/tools && uv run python ../dispenser/make_copy.py

1. Writes ref/{box,lift,m4023}.stl from dispenser.stl: the context assembly.scad imports.
2. Removes the upright 4023, the box and the spare 17HS4401 + screw + nut parked outside the box from a copy of
   dispenser.stl and appends every placed piece of assembly.scad as separate bodies: the box with Dev's changes
   (motor cradle, pusher notch, screw hole), the roller (frame, drums, gears, gates, turned 4023, rods, O-rings) and
   the pusher at home (carriage, 17HS4401, cut screw, nut).
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np
import trimesh

from cad import scad

HERE = Path(__file__).resolve().parent
SRC = HERE / "dispenser.stl"
OUT = HERE / "dispenser-roller.stl"
PIECES = ["box-cut", "frame", "front-plate", "drive-drum", "idler-drum", "motor-gear", "gate-left", "gate-right", "motor",
          "rods", "belts", "pusher", "pusher-motor", "pusher-screw", "pusher-nut"]

# Bodies of dispenser.stl, found by their bounding boxes (mm, min corner, max corner).
REF = {
    "m4023": ([13.85, -50.66, 4.9], [56.15, -3.85, 53.5]),
    "box": ([-5, -165, 0], [150, 0, 200]),
    "lift": ([77.44, -146, 16], [144.44, -14.9, 66]),
}
# Dropped from the copy, not written to ref/: the pusher's motor, screw and nut as Dev parked them.
SPARE = {
    "m4401": ([354, -47, 5], [396, -5, 69]),
    "screw": ([370.9, -30, 1.2], [378.9, -22, 201.2]),
    "nut": ([363.8, -36.4, 47.5], [385.8, -14.4, 62.54]),
}


def find(bodies: list[trimesh.Trimesh], lo, hi) -> int:
    hits = [i for i, b in enumerate(bodies) if np.allclose(b.bounds, [lo, hi], atol=0.05)]
    if len(hits) != 1:
        sys.exit(f"expected 1 body at {lo}..{hi}, found {len(hits)}: dispenser.stl changed?")
    return hits[0]


def main() -> int:
    src = trimesh.load_mesh(SRC)
    labels = trimesh.graph.connected_component_labels(src.face_adjacency, node_count=len(src.faces))
    bodies = [src.submesh([np.flatnonzero(labels == k)], append=True) for k in range(labels.max() + 1)]
    idx = {name: find(bodies, *box) for name, box in REF.items()}
    drop = {idx["m4023"], idx["box"]} | {find(bodies, *box) for box in SPARE.values()}

    (HERE / "ref").mkdir(exist_ok=True)
    for name, i in idx.items():
        bodies[i].export(HERE / "ref" / f"{name}.stl")

    keep = src.submesh([np.flatnonzero(~np.isin(labels, list(drop)))], append=True)
    parts = [keep]
    with tempfile.TemporaryDirectory() as tmp:
        for p in PIECES:
            out = Path(tmp) / f"{p}.stl"
            scad.export_stl(HERE / "assembly.scad", out, {"part": p})
            parts.append(trimesh.load_mesh(out))
            print(f"{p:12} {len(parts[-1].faces):7} faces")
    trimesh.util.concatenate(parts).export(OUT)
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
