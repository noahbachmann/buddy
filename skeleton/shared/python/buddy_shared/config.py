"""Typed BUDDY_* lookups, like shared/src/config.cpp.

Surrounding whitespace is stripped; unset or empty → fallback. Unparsable numbers raise ValueError naming the
variable, so a typo in buddy.env fails loudly at startup.
"""

import os
from dataclasses import dataclass


def env_string(name: str, fallback: str) -> str:
    # trailing spaces and "\r" (buddy.env saved with Windows line endings) are invisible but break IDs and paths
    value = os.environ.get(name, "").strip(" \t\r\n")
    return value or fallback


def _env_number(name: str, fallback, parse):
    value = env_string(name, "")
    if not value:
        return fallback
    try:
        return parse(value)
    except ValueError:
        raise ValueError(f'{name}="{value}" is not a valid number') from None


def env_int(name: str, fallback: int) -> int:
    return _env_number(name, fallback, int)


def env_float(name: str, fallback: float) -> float:
    return _env_number(name, fallback, float)


@dataclass
class MqttConfig:
    """Only the broker address changes when a service moves to another host (D3)."""

    host: str = "localhost"
    port: int = 1883
    user: str = ""
    password: str = ""

    @classmethod
    def from_env(cls) -> "MqttConfig":
        return cls(
            host=env_string("BUDDY_MQTT_HOST", "localhost"),
            port=env_int("BUDDY_MQTT_PORT", 1883),
            user=env_string("BUDDY_MQTT_USER", ""),
            password=env_string("BUDDY_MQTT_PASSWORD", ""),
        )
