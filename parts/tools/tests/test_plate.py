import pytest
import trimesh

from cad import config, plate


def _box(x, y, z=5):
    m = trimesh.creation.box((x, y, z))
    m.apply_translation([100, -50, z / 2 + 3])   # off-origin, above the bed: pack must normalise
    return m


def test_pack_keeps_parts_apart_on_bed_and_centred():
    out = plate.pack([_box(100, 60), _box(100, 40), _box(20, 50)], bed=(230, 230), gap=8)
    bodies = out.split(only_watertight=False)
    assert len(bodies) == 3
    assert out.bounds[0][2] == pytest.approx(0)
    assert (out.bounds[0][:2] + out.bounds[1][:2]) == pytest.approx([0, 0])
    for i, a in enumerate(bodies):   # bounding boxes ≥ gap apart in X or Y
        for b in bodies[i + 1:]:
            dx = max(b.bounds[0][0] - a.bounds[1][0], a.bounds[0][0] - b.bounds[1][0])
            dy = max(b.bounds[0][1] - a.bounds[1][1], a.bounds[0][1] - b.bounds[1][1])
            assert max(dx, dy) >= 8 - 1e-6


def test_pack_turns_only_about_z():
    out = plate.pack([_box(30, 120, 7)], bed=(230, 230))
    assert out.extents == pytest.approx([120, 30, 7])   # long side along X, height kept


def test_pack_fills_a_row_with_a_turned_part():
    # 150 × 100 leaves 72 in its row: the 90 × 20 only fits there turned (20 × 90)
    out = plate.pack([_box(150, 100), _box(90, 20)], bed=(230, 230), gap=8)
    assert out.extents[1] == pytest.approx(100)


def test_pack_too_big():
    with pytest.raises(ValueError, match="plate full"):
        plate.pack([_box(200, 150), _box(200, 150)], bed=(230, 230))


def test_config_plates(tmp_path):
    (tmp_path / "a.scad").write_text("cube(1);")
    (tmp_path / config.CONFIG).write_text(
        '[parts.a]\n[parts.b]\nfile = "a.scad"\n[plates.p1]\nnote = "PLA"\nparts = ["a", "a", ["a", "b"]]\n')
    mod = config.load(tmp_path)
    assert mod.plates["p1"].items == [["a"], ["a"], ["a", "b"]]
    assert mod.plates["p1"].note == "PLA"
    (tmp_path / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a", "zz"]\n')
    with pytest.raises(SystemExit, match="zz"):
        config.load(tmp_path)
