from pathlib import Path

import pytest

from cad import mesh, scad

FIX = Path(__file__).parent / "fixtures"


@pytest.fixture(scope="session")
def bracket(tmp_path_factory):
    """Builds the bracket fixture once per defect; returns defect → Trimesh."""
    out = tmp_path_factory.mktemp("bracket")
    cache = {}

    def get(defect: str = "none"):
        if defect not in cache:
            stl = out / f"{defect}.stl"
            scad.export_stl(FIX / "bracket.scad", stl, {"defect": defect})
            cache[defect] = mesh.load(stl)
        return cache[defect]

    return get
