import json
from pathlib import Path

import pytest

from buddy_shared.config import MqttConfig, env_float, env_int, env_string

CASES = json.loads((Path(__file__).parents[2] / "tests/cases/env_values.json").read_text())
FALLBACK = {"string": "fallback", "int": 1234, "float": 12.5}
GET = {"string": env_string, "int": env_int, "float": env_float}


@pytest.mark.parametrize("kind, value, want", [(k, v, w) for k in GET for v, w in CASES[k]])
def test_shared_cases(monkeypatch, kind, value, want):
    monkeypatch.setenv("BUDDY_TEST_VALUE", value)
    if want == "error":
        with pytest.raises(ValueError, match="BUDDY_TEST_VALUE"):
            GET[kind]("BUDDY_TEST_VALUE", FALLBACK[kind])
    else:
        assert GET[kind]("BUDDY_TEST_VALUE", FALLBACK[kind]) == (FALLBACK[kind] if want is None else want)


def test_unset_gives_fallback(monkeypatch):
    monkeypatch.delenv("BUDDY_TEST_VALUE", raising=False)
    assert env_int("BUDDY_TEST_VALUE", 7) == 7


def test_mqtt_defaults(monkeypatch):
    monkeypatch.delenv("BUDDY_MQTT_HOST", raising=False)
    assert MqttConfig.from_env().host == "localhost"
