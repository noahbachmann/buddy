import base64
import random

import pytest

from buddy_screener.device import AdbError, Device, NotIdle, Stopped
from buddy_screener.uitree import Rect


class Runner:
    """Canned adb answers by the command's first words after the serial."""

    def __init__(self, *answers):
        self.answers = list(answers)
        self.calls: list[list[str]] = []

    def __call__(self, cmd, stdin, timeout):
        self.calls.append(cmd)
        return self.answers.pop(0)


def dev(runner, guard=lambda: None) -> Device:
    return Device("SER", guard=guard, run=runner, rng=random.Random(1), sleep=lambda s: None)


def test_dump_retries_idle_state_failures():
    r = Runner((0, b"ERROR: could not get idle state.", b""),
               (0, b"<hierarchy><node/></hierarchy>UI hierchary dumped to: /dev/tty", b""))
    assert dev(r).dump().startswith("<hierarchy>")
    assert r.calls[0][:3] == ["adb", "-s", "SER"]
    assert len(r.calls) == 2


def test_dump_gives_up_on_a_screen_that_never_idles():
    r = Runner(*[(0, b"ERROR: could not get idle state.", b"")] * 5)
    with pytest.raises(NotIdle):
        dev(r).dump()
    assert len(r.calls) == 2   # each try already waited ~10 s for idle: a video keeps playing


def test_dump_gives_up_on_other_failures():
    r = Runner(*[(0, b"ERROR: null root node returned by UiTestAutomationBridge.", b"")] * 5)
    with pytest.raises(AdbError) as e:
        dev(r).dump()
    assert not isinstance(e.value, NotIdle) and len(r.calls) == 5


def test_tap_stays_inside_the_middle_of_the_target():
    target = Rect(846, 1582, 990, 1726)
    for seed in range(20):
        r = Runner((0, b"", b""))
        d = Device(guard=lambda: None, run=r, rng=random.Random(seed))
        x, y = d.tap(target)
        assert 846 + 36 <= x <= 990 - 36 and 1582 + 36 <= y <= 1726 - 36


def test_guard_runs_before_any_input():
    r = Runner()

    def stop():
        raise Stopped("stopped")
    d = dev(r, stop)
    for act in (lambda: d.tap(Rect(0, 0, 10, 10)), lambda: d.swipe(1, 2, 3, 4), lambda: d.key(4),
                lambda: d.launch("x"), lambda: d.ime_type("t"), lambda: d.ime_clear()):
        with pytest.raises(Stopped):
            act()
    assert r.calls == []


def test_keyboard_answers():
    text = "Grüezi – 🙂"
    ok = f'Broadcasting: Intent {{ }}\nBroadcast completed: result=1, data="{base64.b64encode(text.encode()).decode()}"\n'
    r = Runner((0, ok.encode(), b""), (0, b'Broadcast completed: result=0, data="no_field"\n', b""),
               (0, b"Broadcast completed: result=0\n", b""))
    d = dev(r)
    res = d.ime_type(text)
    assert res.ok and res.text == text
    assert r.calls[0][-2:] == ["b64", base64.b64encode(text.encode()).decode()]
    assert d.ime_read() == type(res)(False, "no_field")
    assert not d.ime_clear().ok    # nobody answered: receiver not running


def test_state_and_foreground():
    r = Runner((1, b"", b"error: device unauthorized."),
               (0, b"  mCurrentFocus=Window{d4bda8f u0 app.example/app.example.ui.AppActivity}\n", b""))
    d = dev(r)
    assert d.state() == "unauthorized"
    assert d.foreground() == "app.example"


@pytest.mark.parametrize("out, size", [(b"Physical size: 1080x2280\n", (1080, 2280)),
                                       (b"Physical size: 1080x2280\nOverride size: 720x1520\n", (720, 1520))])
def test_size(out, size):
    assert dev(Runner((0, out, b""))).size() == size
