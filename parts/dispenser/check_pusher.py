"""Collision check of the pusher over its whole stroke, with the lift at push height.

    cd parts/tools && uv run python ../dispenser/check_pusher.py

Exports the placed pieces from assembly.scad, intersects the carriage and its nut (9 positions) with everything fixed
(box with its changes, lift at push height, roller, motor, screw, a full pile (pile_max) on the lift), and the lift with
its nut (lowest, 2 between, push height) and the pile with the box, the wall extension, motors, couplings, screw and
the carriage at home. Exit 1 on any overlap.
"""

from __future__ import annotations

import itertools
import re
import sys
import tempfile
from pathlib import Path

import manifold3d as m3
import numpy as np
import trimesh

from cad import scad

HERE = Path(__file__).resolve().parent
PUSHES = [0, 0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0]
FIXED = ["box-cut", "frame", "front-plate", "gate-left", "gate-right", "drive-drum", "idler-drum",
         "pusher-motor", "pusher-coupling", "pusher-screw"]
# what the lift passes: the pusher at home and the lift's own motor and coupling (its screw runs through the nut)
HOME = ["box-cut", "case-add", "pusher-motor", "pusher-coupling", "pusher-screw", "pusher", "pusher-nut", "lift-motor",
        "lift-coupling"]
LIFT_BETWEEN = [50, 75]   # + lift_min and push_lift


def param(tmp: Path, expr: str) -> float:
    src = tmp / "echo.scad"
    src.write_text(f'include <{HERE / "params.scad"}>\necho(v = {expr});\ncube(1);\n')
    log = scad.run(src, tmp / "echo.stl")
    return float(re.search(r"ECHO: v = ([-\d.e]+)", log).group(1))


def solid(t: trimesh.Trimesh) -> m3.Manifold:
    return m3.Manifold(m3.Mesh(vert_properties=np.asarray(t.vertices, np.float32),
                               tri_verts=np.asarray(t.faces, np.uint32)))


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        push_lift = param(tmp, "push_lift")
        lift_min = param(tmp, "lift_min")
        card_y = [param(tmp, "card_yc - card[1] / 2"), param(tmp, "card_yc + card[1] / 2")]

        def piece(name: str, **defs) -> m3.Manifold:
            out = tmp / f"{name}-{len(list(tmp.iterdir()))}.stl"
            scad.export_stl(HERE / "assembly.scad", out, {"part": name, "lift_top": push_lift, **defs})
            mesh = trimesh.load_mesh(out)
            if not mesh.is_watertight:
                sys.exit(f"{name}: not watertight, can't check it")
            return solid(mesh)

        def lift(top: float) -> m3.Manifold:
            return piece("lift", lift_top=top) + piece("lift-nut", lift_top=top)

        fixed = {n: piece(n) for n in FIXED}
        fixed["lift"] = lift(push_lift)
        pile_max = param(tmp, "pile_max")
        pile = solid(trimesh.creation.box(bounds=[[82, card_y[0], push_lift], [145, card_y[1], push_lift + pile_max]]))
        hits = []
        for push in PUSHES:
            moving = {"pusher": piece("pusher", push=push), "pusher-nut": piece("pusher-nut", push=push)}
            others = {**fixed, **({"pile": pile} if push == 0 else {})}   # later the pile moves with the plate
            for (a, ma), (b, mb) in itertools.product(moving.items(), others.items()):
                if (a, b) != ("pusher-nut", "pusher-screw"):
                    hits.append((f"push {push}: {a} x {b}", (ma ^ mb).volume()))
        home = {n: fixed[n] if n in fixed else piece(n) for n in HOME}
        lifts = [(f"lift at {t:.2f}", lift(t)) for t in [lift_min, *LIFT_BETWEEN, push_lift]]
        for (a, ma), (b, mb) in itertools.product([*lifts, ("pile", pile)], home.items()):
            hits.append((f"{a} x {b}", (ma ^ mb).volume()))
        # self-test: 1 mm too high a lift must hit the plate
        canary = (piece("pusher", push=0.5) ^ lift(push_lift + 1)).volume()
    bad = [(n, v) for n, v in hits if v > 1e-3]
    for n, v in bad:
        print(f"{n}: {v:.2f} mm³")
    print(f"{len(hits)} pairs, {len(bad)} overlapping; lift at z {push_lift:.2f}; canary (lift +1 mm) {canary:.0f} mm³")
    return 1 if bad or canary < 1 else 0


if __name__ == "__main__":
    sys.exit(main())
