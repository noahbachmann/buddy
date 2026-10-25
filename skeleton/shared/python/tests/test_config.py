import pytest

from buddy_shared.config import MqttConfig, env_float, env_int, env_string


def test_strings_are_trimmed(monkeypatch):
    monkeypatch.setenv("BUDDY_X", "  value\r")
    assert env_string("BUDDY_X", "fallback") == "value"
    monkeypatch.setenv("BUDDY_X", " \r")
    assert env_string("BUDDY_X", "fallback") == "fallback"


def test_numbers(monkeypatch):
    monkeypatch.setenv("BUDDY_N", "42")
    assert env_int("BUDDY_N", 1) == 42
    assert env_float("BUDDY_N", 1.0) == 42.0
    assert env_int("BUDDY_UNSET", 7) == 7


def test_typo_raises_with_name(monkeypatch):
    monkeypatch.setenv("BUDDY_N", "4o")
    with pytest.raises(ValueError, match="BUDDY_N"):
        env_int("BUDDY_N", 1)


def test_mqtt_defaults(monkeypatch):
    monkeypatch.delenv("BUDDY_MQTT_HOST", raising=False)
    assert MqttConfig.from_env().host == "localhost"
