"""Needs a broker on localhost:1883 (the dev VM has Mosquitto); skipped otherwise."""

import socket
import threading

import pytest

from buddy_shared import lifecycle
from buddy_shared.config import MqttConfig
from buddy_shared.mqtt import Mqtt


def broker_up() -> bool:
    try:
        socket.create_connection(("localhost", 1883), timeout=0.5).close()
        return True
    except OSError:
        return False


pytestmark = pytest.mark.skipif(not broker_up(), reason="no MQTT broker on localhost:1883")


def test_status_roundtrip_and_handler_errors():
    cfg = MqttConfig()
    got: list[tuple[str, str]] = []
    done = threading.Event()

    def collect(topic: str, payload: str) -> None:
        got.append((topic, payload))
        if ("buddy/pytest-svc/status", "online") in got and payload == "offline":  # not a retained leftover
            done.set()

    subscribed = threading.Event()
    watcher = Mqtt("pytest-watcher", cfg)
    watcher.subscribe("buddy/pytest-svc/#", collect)
    watcher.subscribe("buddy/pytest-svc/#", lambda *_: 1 / 0)  # must be logged, not kill paho's thread
    watcher.subscribe("buddy/pytest-watcher/probe", lambda *_: subscribed.set())
    assert watcher.start()
    # start() returns before the broker acks the subscriptions: the probe coming back proves they're active
    for _ in range(30):
        watcher.publish("buddy/pytest-watcher/probe", "")
        if subscribed.wait(0.1):
            break
    assert subscribed.is_set()

    svc = Mqtt("pytest-svc", cfg)
    assert svc.start()
    svc.publish("buddy/pytest-svc/event", '{"x":1}')
    svc.stop()

    assert done.wait(3)
    watcher.publish("buddy/pytest-svc/status", "", qos=1, retain=True)  # clear the retained status
    watcher.stop()
    assert ("buddy/pytest-svc/status", "online") in got
    assert ("buddy/pytest-svc/event", '{"x":1}') in got
    assert lifecycle.running()
