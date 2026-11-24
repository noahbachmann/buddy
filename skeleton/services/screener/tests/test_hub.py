"""The service side of the loop (#80): holds on Telegram, Dev's answers, the state payload, the hold guard."""

import json

import pytest

from buddy_screener import browse
from buddy_screener.config import Config
from buddy_screener.device import Stopped
from buddy_screener.hub import HOLD_TEXT, Hub
from buddy_screener.main import handle, state_payload
from buddy_screener.store import Store


@pytest.fixture
def cfg(tmp_path):
    return Config(package="app.example", config_dir=tmp_path / "c", data_dir=tmp_path / "d", browse="dry")


def test_every_hold_says_what_happened_and_what_to_do(cfg):
    sent = []
    hub = Hub(cfg, lambda topic, msg: sent.append(msg))
    for kind in (browse.HOLD_WOULD_LIKE, browse.HOLD_SCREEN, browse.HOLD_LIKES, browse.HOLD_DECIDE, browse.HOLD_FILES):
        hub.hold(kind, 12, "waited for the app, got unknown")
        text = sent[-1]["text"]
        assert "{" not in text and "To do:" in text
        assert kind == browse.HOLD_WOULD_LIKE or "waited for the app" in text
    hub.hold(browse.HOLD_SCREEN, None, "x " * 300)
    assert len(sent[-1]["text"]) < 600


def test_hold_asks_dev_and_the_answer_resumes(cfg):
    sent = []
    hub = Hub(cfg, lambda topic, msg: sent.append((topic, msg)), clock=lambda: 1.5)
    hub.hold(browse.HOLD_WOULD_LIKE, 12, "")
    assert cfg.mode() == "needs_you"
    topic, ask = sent[0]
    assert topic == "buddy/messenger/cmd" and ask["action"] == "ask" and ask["options"] == ["Resume", "Stop"]
    assert "#12" in ask["text"] and ask["id"] == hub.held()["ask"]
    assert hub.answer({"event": "answered", "id": "other", "option": 0}) is None
    assert hub.answer({"event": "asked", "id": ask["id"]}) is None
    assert hub.answer({"event": "answered", "id": ask["id"], "text": "hm"}) is None   # typed, no button
    assert hub.answer({"event": "answered", "id": ask["id"], "option": 1}) == "stop"
    assert hub.answer({"event": "answered", "id": ask["id"], "option": 0}) == "resume"
    hub.resumed()
    assert cfg.mode() == "active"
    assert sent[-1][1] == {"action": "cancel", "id": ask["id"], "note": "▶ resumed on the panel"}


def test_a_hold_stops_the_loops_input_but_not_devs(cfg):
    Hub(cfg, lambda *a: None).hold(browse.HOLD_SCREEN, None)
    with pytest.raises(Stopped, match="needs_you"):
        cfg.guard()
    cfg.guard(hold_ok=True)
    cfg.config_dir.mkdir()
    cfg.stop_file.touch()
    with pytest.raises(Stopped, match="stopped"):
        cfg.guard(hold_ok=True)


class Dev:
    def ime_current(self):
        return "buddy.screener.ime/.ScreenerIme"

    def ime_list(self):
        return [self.ime_current()]


def test_resume_clears_stop_pause_and_hold(cfg, monkeypatch):
    from buddy_screener import main
    monkeypatch.setattr(main, "OUR_IME", Dev().ime_current())
    sent = []
    hub = Hub(cfg, lambda topic, msg: sent.append(msg))
    hub.hold(browse.HOLD_SCREEN, None)
    assert handle(cfg, Dev(), "stop", hub) is None and cfg.mode() == "stopped"
    assert handle(cfg, Dev(), "resume", hub, "by Claude") is None and cfg.mode() == "active"
    assert sent[-1] == {"action": "cancel", "id": sent[0]["id"], "note": "▶ resumed by Claude"}


def test_state_payload_has_counts_and_no_names(cfg):
    store = Store(":memory:")
    store.add_action("skip", "dry", 1)
    store.add_action("would_like", "dry", 2, "a like waits for Dev")
    b = type("B", (), {})()
    b.state = browse.LoopState("browsing", None, "", {"action": "skip", "ts": 1})
    out = state_payload(cfg, store, b, "ok")
    assert out["state"] == "browsing" and out["mode"] == "dry" and out["last"]["action"] == "skip"
    assert out["today"] == {"profiles": 2, "skips": 1, "likes": 0, "would_like": 1, "likes_left": 8, "likes_max": 8}
    Hub(cfg, lambda *a: None).hold(browse.HOLD_WOULD_LIKE, 2)
    b.state.reason = browse.HOLD_WOULD_LIKE
    out = state_payload(cfg, store, b, "ok")
    assert (out["state"], out["hold"]) == ("needs_you", "would_like")
    assert "a like waits" not in json.dumps(out)
