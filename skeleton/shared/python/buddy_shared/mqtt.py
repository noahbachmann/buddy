"""Thin wrapper around paho-mqtt, like shared/src/mqtt.cpp:

- client id "buddy-<service>": only one instance per service can be connected
- last will: buddy/<service>/status = "offline" (retained)
- on every (re)connect: publishes "online" and (re)subscribes
- automatic reconnect; the first connect retries until the broker is reachable
- start() tells systemd READY=1 (call it after loading models/config) and keeps the watchdog fed while it waits
- exceptions from handlers and publish() are logged, never raised into paho's thread
"""

import threading
import time
from collections.abc import Callable

import paho.mqtt.client as paho
from paho.mqtt.enums import CallbackAPIVersion

from . import lifecycle, systemd, topics
from .config import MqttConfig

Handler = Callable[[str, str], None]  # (topic, payload)


class Mqtt:
    def __init__(self, service: str, cfg: MqttConfig | None = None):
        self.service = service
        self.cfg = cfg or MqttConfig.from_env()
        self._status = topics.status(service)
        self._subs: list[tuple[str, Handler]] = []
        self._connected = threading.Event()
        self._client = paho.Client(CallbackAPIVersion.VERSION2, client_id=f"buddy-{service}", clean_session=True)
        if self.cfg.user:
            self._client.username_pw_set(self.cfg.user, self.cfg.password)
        self._client.will_set(self._status, "offline", qos=1, retain=True)
        self._client.reconnect_delay_set(min_delay=1, max_delay=30)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    def log(self, text: str) -> None:
        print(f"[{self.service}] {text}", flush=True)

    def subscribe(self, filter: str, handler: Handler) -> None:
        """Register before start(). Filters may use MQTT wildcards. Handlers run on paho's thread: keep them short."""
        self._subs.append((filter, handler))

    def start(self) -> bool:
        """Blocks until connected or shutdown was requested. Returns False on shutdown."""
        # Every service builds Mqtt after loading its models/config: from here on it only waits for the broker.
        systemd.notify_ready()
        server = f"{self.cfg.host}:{self.cfg.port}"

        # At boot the user units start before Mosquitto: retry fast first, then back off. Log the first failure
        # and then only every 30 s (journald, microSD).
        delay, next_log = 0.25, 0.0
        while lifecycle.running():
            systemd.notify_watchdog()
            try:
                self._client.connect(self.cfg.host, self.cfg.port, keepalive=10)
                break
            except OSError as e:
                if time.monotonic() >= next_log:
                    self.log(f"connect to {server} failed: {e}, retrying")
                    systemd.notify_status("waiting for the broker")
                    next_log = time.monotonic() + 30
                lifecycle.sleep(delay)
                delay = min(delay * 2, 5.0)
        else:
            return False

        self._client.loop_start()  # network thread; reconnects by itself from now on
        while lifecycle.running():
            systemd.notify_watchdog()
            if self._connected.wait(0.2):
                return True
        return False

    def publish(self, topic: str, payload: str, qos: int = 0, retain: bool = False) -> None:
        """Dropped while disconnected (fine for events)."""
        if not self._connected.is_set():
            return
        try:
            self._client.publish(topic, payload, qos=qos, retain=retain)
        except Exception as e:  # connection can drop between the check and the call
            self.log(f"publish to {topic} failed: {e}")

    def stop(self) -> None:
        if self._connected.is_set():
            # Bounded waits: a broker that dies during shutdown mustn't hang `systemctl stop`.
            try:
                self._client.publish(self._status, "offline", qos=1, retain=True).wait_for_publish(2)
                self._client.disconnect()
                self.log("disconnected")
            except Exception as e:
                self.log(f"disconnect failed: {e}")
        self._client.loop_stop()

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            self.log(f"broker refused the connection: {reason_code}")
            return
        self.log(f"connected to {self.cfg.host}:{self.cfg.port}")
        systemd.notify_status("connected")
        self._connected.set()
        self.publish(self._status, "online", qos=1, retain=True)
        for filter, _ in self._subs:
            client.subscribe(filter, qos=1)

    def _on_disconnect(self, client, userdata, flags, reason_code, properties) -> None:
        self._connected.clear()
        if reason_code != 0:  # 0 = our own disconnect()
            self.log(f"connection lost ({reason_code}), reconnecting")
            systemd.notify_status("reconnecting to the broker")

    # Runs on paho's thread: an exception escaping here would stop its network loop.
    def _on_message(self, client, userdata, msg) -> None:
        payload = msg.payload.decode(errors="replace")
        for filter, handler in self._subs:
            if not topics.matches(filter, msg.topic):
                continue
            try:
                handler(msg.topic, payload)
            except Exception as e:
                self.log(f"handler for {msg.topic} failed: {e}")
