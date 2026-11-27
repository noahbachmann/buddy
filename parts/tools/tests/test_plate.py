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
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "a.scad").write_text("cube(1);")
    (tmp_path / "src" / config.CONFIG).write_text(
        '[parts.a]\n[parts.b]\nfile = "a.scad"\n[plates.p1]\nnote = "PLA"\nparts = ["a", "a", ["a", "b"]]\n')
    mod = config.load(tmp_path)
    assert mod.plates["p1"].items == [["a"], ["a"], ["a", "b"]]
    assert mod.plates["p1"].note == "PLA"
    (tmp_path / "src" / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a", "zz"]\n')
    with pytest.raises(SystemExit, match="zz"):
        config.load(tmp_path)


def test_config_plate_bed(tmp_path):
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "a.scad").write_text("cube(1);")
    (tmp_path / "src" / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a"]\nbed = [254, 250]\n[plates.p2]\nparts = ["a"]\n')
    mod = config.load(tmp_path)
    assert mod.plates["p1"].bed == (254.0, 250.0)
    assert mod.plates["p2"].bed is None
    (tmp_path / "src" / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a"]\nbed = [300, 250]\n')
    with pytest.raises(SystemExit, match="bed"):
        config.load(tmp_path)


def test_pack_wide_part_needs_wide_bed():
    with pytest.raises(ValueError, match="plate full"):
        plate.pack([_box(250, 220)])
    assert plate.pack([_box(250, 220)], bed=(254, 254)).extents[0] == pytest.approx(250)


def _ell():   # 100 × 100 L: the 60 × 60 corner at min x, min y cut away
    a = trimesh.creation.box((100, 40, 5)); a.apply_translation([50, 80, 2.5])
    b = trimesh.creation.box((40, 60, 5)); b.apply_translation([80, 30, 2.5])
    return trimesh.util.concatenate([a, b])


def test_place_nests_into_an_l():
    # 50 × 50 box in the L's cut corner, 5 apart: shelves couldn't, bbox overlap is fine
    out = plate.place([_box(50, 50), _ell()], [(0, 0, 0), (-5, -5, 0)], bed=(230, 230), gap=5)
    assert out.extents[:2] == pytest.approx([100, 100])
    assert out.bounds[0][2] == pytest.approx(0)


def test_place_turns_and_checks_gap_and_bed():
    out = plate.place([_box(60, 20)], [(0, 0, 90)])
    assert out.extents[:2] == pytest.approx([20, 60])
    with pytest.raises(ValueError, match="apart"):
        plate.place([_box(50, 50), _ell()], [(0, 0, 0), (-6, -6, 0)], gap=5)
    with pytest.raises(ValueError, match="plate full"):
        plate.place([_box(100, 10), _box(100, 10)], [(0, 0, 0), (140, 0, 0)])


def test_config_plate_place(tmp_path):
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "a.scad").write_text("cube(1);")
    (tmp_path / "src" / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a", "a"]\nplace = [[0, 0, 90], [5, 6, 0]]\ngap = 4\n')
    p = config.load(tmp_path).plates["p1"]
    assert p.place == [(0, 0, 90), (5, 6, 0)] and p.gap == 4
    (tmp_path / "src" / config.CONFIG).write_text('[parts.a]\n[plates.p1]\nparts = ["a", "a"]\nplace = [[0, 0, 90]]\n')
    with pytest.raises(SystemExit, match="place"):
        config.load(tmp_path)


def test_find_module_from_any_folder(tmp_path):
    (tmp_path / "src" / "ref").mkdir(parents=True)
    (tmp_path / "stl").mkdir()
    (tmp_path / "src" / "a.scad").write_text("cube(1);")
    (tmp_path / "src" / config.CONFIG).write_text("[parts.a]\n")
    for p in (tmp_path, tmp_path / "src" / "a.scad", tmp_path / "src" / "ref", tmp_path / "stl"):
        mod = config.find_module(p)
        assert mod is not None and mod.dir == tmp_path.resolve()
    assert mod.plate_dir == mod.dir and mod.render_dir == mod.dir / "src" / "renders"
