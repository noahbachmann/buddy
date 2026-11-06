import pytest

from cad import config, scad
from conftest import FIX


def test_warning_fails_build(tmp_path):
    with pytest.raises(scad.ScadError, match="WARNING"):
        scad.export_stl(FIX / "warns.scad", tmp_path / "w.stl")


def test_literals():
    assert scad.literal("a\"b") == '"a\\"b"'
    assert scad.literal(True) == "true"
    assert scad.literal([1, 2.5]) == "[1,2.5]"
    assert scad.parse_define("elbow=60") == ("elbow", "60")


def test_module_config():
    mod = config.load(FIX / "mod")
    assert list(mod.parts) == ["bracket", "shelf"]
    assert mod.parts["shelf"].defines == {"defect": "shelf"}
    with pytest.raises(SystemExit, match="unknown part"):
        mod.select(["nope"])
