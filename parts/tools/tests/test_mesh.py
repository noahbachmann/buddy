import math

import pytest

from cad import mesh


def test_clean_bracket(bracket):
    st = mesh.stats(bracket())
    assert st.watertight
    assert st.size == pytest.approx((40, 20, 40))
    assert st.overhang_mm2 == pytest.approx(0, abs=0.5)  # teardrop: exactly 45°, passes
    assert st.roof_mm2 == pytest.approx(0, abs=0.5)
    hole = math.pi * 1.7**2
    assert st.bed_mm2 == pytest.approx(40 * 20 - 2 * hole, rel=0.01)


def test_round_hole_overhangs(bracket):
    assert mesh.stats(bracket("round_hole")).overhang_mm2 > 20


def test_shelf_is_roof(bracket):
    st = mesh.stats(bracket("shelf"))
    assert st.roof_mm2 == pytest.approx(10 * 20, rel=0.001)
    assert st.overhang_mm2 == pytest.approx(0, abs=0.5)


def test_45_chamfer_passes(bracket):
    st = mesh.stats(bracket("chamfer45"))
    assert st.overhang_mm2 == pytest.approx(0, abs=0.5)
    assert st.roof_mm2 == pytest.approx(0, abs=0.5)


def test_missing_hole_more_bed(bracket):
    assert mesh.stats(bracket("missing_hole")).bed_mm2 > mesh.stats(bracket()).bed_mm2 + 8
