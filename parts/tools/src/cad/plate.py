"""Print plates: a module's parts packed onto the build plate, one STL per print job.

Each part keeps its print orientation and only turns about Z (0 or 90°). Items go into shelves (rows along X),
largest first; an item joins the first row it fits into, turned if that fits better. Shelves can't nest an L into
another part's corner: `place()` takes fixed spots instead and checks the real footprints keep the gap. The result
is centred on the origin, so a slicer that centres on import puts it mid-bed.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import trimesh

BED = (230.0, 230.0)    # usable mm: X2D is 256 × 256 single-nozzle, 235.5 wide with both; margin for brims
GAP = 8.0               # mm between parts: room for a brim, a scraper and a slicer's auto-arrange


@dataclass
class _Row:
    y: float
    depth: float
    x: float = 0.0


def _footprint(m: trimesh.Trimesh) -> tuple[float, float]:
    e = m.extents
    return float(e[0]), float(e[1])


def _turn(m: trimesh.Trimesh, deg: float = 90) -> trimesh.Trimesh:
    t = m.copy()
    t.apply_transform(trimesh.transformations.rotation_matrix(np.radians(deg), [0, 0, 1]))
    return t


def _centred(placed: list[trimesh.Trimesh]) -> trimesh.Trimesh:
    out = trimesh.util.concatenate(placed)
    c = (out.bounds[0] + out.bounds[1]) / 2
    out.apply_translation([-c[0], -c[1], 0])
    return out


def place(items: list[trimesh.Trimesh], spots: list[tuple[float, float, float]], bed: tuple[float, float] = BED,
          gap: float = GAP) -> trimesh.Trimesh:
    """Fixed layout: item i turned by spots[i][2] degrees about Z, its bounding box's min corner at spots[i][:2].
    Raises if the layout leaves the bed or two footprints come closer than `gap`."""
    from trimesh.path.polygons import projected
    if len(spots) != len(items):
        raise ValueError(f"place: {len(spots)} spots for {len(items)} items")
    placed = []
    for m, (x, y, deg) in zip(items, spots):
        m = _turn(m, deg)
        lo = m.bounds[0]
        m.apply_translation([x - lo[0], y - lo[1], -lo[2]])
        placed.append(m)
    out = trimesh.util.concatenate(placed)
    w, d = _footprint(out)
    if w > bed[0] + 1e-9 or d > bed[1] + 1e-9:
        raise ValueError(f"plate full: layout {w:.0f} × {d:.0f} mm doesn't fit on {bed[0]:.0f} × {bed[1]:.0f}")
    fps = [projected(m, normal=[0, 0, 1]) for m in placed]
    for i in range(len(fps)):
        for j in range(i + 1, len(fps)):
            if (dist := fps[i].distance(fps[j])) < gap - 1e-6:
                raise ValueError(f"place: items {i + 1} and {j + 1} are {dist:.1f} mm apart, need {gap:.0f}")
    return _centred(placed)


def pack(items: list[trimesh.Trimesh], bed: tuple[float, float] = BED, gap: float = GAP) -> trimesh.Trimesh:
    """Lay `items` out on the bed; each item is one rigid mesh (may hold several bodies). Raises if they don't fit."""
    # long side along X: rows stay shallow
    items = [m if _footprint(m)[0] >= _footprint(m)[1] else _turn(m) for m in items]
    order = sorted(range(len(items)), key=lambda i: -_footprint(items[i])[1])
    rows: list[_Row] = []
    placed: list[trimesh.Trimesh] = []
    for i in order:
        cands = [items[i], _turn(items[i])]
        spot = None
        for r in rows:
            for m in cands:
                w, d = _footprint(m)
                if d <= r.depth + 1e-9 and r.x + w <= bed[0] + 1e-9:
                    spot = (r, m)
                    break
            if spot:
                break
        if spot is None:
            m = next((c for c in cands if _footprint(c)[0] <= bed[0]), None)
            y = rows[-1].y + rows[-1].depth + gap if rows else 0.0
            if m is None or y + _footprint(m)[1] > bed[1] + 1e-9:
                raise ValueError(f"plate full: item {i + 1} ({_footprint(items[i])[0]:.0f} × "
                                 f"{_footprint(items[i])[1]:.0f} mm) doesn't fit on {bed[0]:.0f} × {bed[1]:.0f}")
            rows.append(_Row(y, _footprint(m)[1]))
            spot = (rows[-1], m)
        r, m = spot
        lo = m.bounds[0]
        m = m.copy()
        m.apply_translation([r.x - lo[0], r.y - lo[1], -lo[2]])
        r.x += _footprint(m)[0] + gap
        placed.append(m)
    return _centred(placed)
