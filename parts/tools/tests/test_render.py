import math

import pytest
from PIL import Image

from cad import cli, render


def test_part_sheet(bracket, tmp_path):
    out = render.part_sheet(bracket(), "bracket", tmp_path / "s.png")
    w, h = Image.open(out).size
    assert (w, h) == (render.CELL[0] * 3, render.CELL[1] * 3 + render.HEADER)


def test_section_through_plate(bracket, tmp_path):
    # z=2 cuts the plate (40×20 minus two Ø3.4 holes) and the wall (4×20, joined to the plate)
    sec = bracket().section(plane_origin=[0, 0, 2], plane_normal=[0, 0, 1])
    planar, _ = sec.to_2D()
    assert planar.area == pytest.approx(40 * 20 - 2 * math.pi * 1.7**2, rel=0.01)
    assert render.section(bracket(), "z", 2, "bracket", tmp_path / "z2.png").is_file()


def test_cli_module_render(tmp_path, capsys):
    from conftest import FIX

    assert cli.main(["render", str(FIX / "mod"), "shelf", "--section", "y=10", "-o", str(tmp_path)]) == 0
    printed = capsys.readouterr().out.split()
    assert [p.rsplit("/", 1)[1] for p in printed] == ["shelf.png", "shelf-y10.png"]
