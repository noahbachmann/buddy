import base64
import gzip
import random
import socket
import threading

import pytest

from buddy_screener import device as device_mod
from buddy_screener.device import AdbError, Device, NotIdle, Stopped, _Served
from buddy_screener.uitree import Rect


class Runner:
    """Canned adb answers by the command's first words after the serial."""

    def __init__(self, *answers):
        self.answers = list(answers)
        self.calls: list[list[str]] = []

    def __call__(self, cmd, stdin, timeout):
        self.calls.append(cmd)
        return self.answers.pop(0)


def dev(runner, guard=lambda: None, dumper="") -> Device:
    return Device("SER", guard=guard, run=runner, rng=random.Random(1), sleep=lambda s: None, dumper=dumper,
                  serve=False)


def own(xml: str) -> bytes:
    return (f"INSTRUMENTATION_RESULT: xml={base64.b64encode(gzip.compress(xml.encode())).decode()}\n"
            "INSTRUMENTATION_CODE: -1\n").encode()


def test_dump_reads_with_our_dumper():
    r = Runner((0, own("<hierarchy rotation=\"0\"><node text=\"Grüezi 😀\"/></hierarchy>"), b""))
    d = dev(r, dumper="buddy.screener.dump/.Dump")
    assert d.dump() == "<hierarchy rotation=\"0\"><node text=\"Grüezi 😀\"/></hierarchy>"
    assert r.calls[0][3:] == ["shell", "am", "instrument", "-w", "buddy.screener.dump/.Dump"]


def test_dump_falls_back_to_uiautomator_when_the_dumper_is_missing():
    missing = (b"INSTRUMENTATION_STATUS: Error=Unable to find instrumentation info for: ComponentInfo{x/.Dump}\n"
               b"android.util.AndroidException: INSTRUMENTATION_FAILED: x/.Dump\n")
    ui = (0, b"<hierarchy><node/></hierarchy>UI hierchary dumped to: /dev/tty", b"")
    r = Runner((0, missing, b""), ui, ui)
    d = dev(r, dumper="buddy.screener.dump/.Dump")
    assert d.dump() == "<hierarchy><node/></hierarchy>" and d.dumper == ""
    d.dump()
    assert len(r.calls) == 3   # missing once: uiautomator from then on


def test_dump_retries_dumper_errors():
    r = Runner((0, b"INSTRUMENTATION_RESULT: error=IllegalStateException: no_window\n", b""),
               (0, own("<hierarchy/>"), b""))
    assert dev(r, dumper="d/.Dump").dump() == "<hierarchy/>"
    r = Runner(*[(0, b"INSTRUMENTATION_RESULT: error=IllegalStateException: no_window\n", b"")] * 5)
    with pytest.raises(AdbError) as e:
        dev(r, dumper="d/.Dump").dump()
    assert not isinstance(e.value, NotIdle) and "no_window" in str(e.value)


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


def test_swipes_never_repeat_and_stay_in_bounds():
    seen = set()
    for seed in range(50):
        r = Runner((0, b"", b""), (0, b"", b""))
        d = Device(guard=lambda: None, run=r, rng=random.Random(seed))
        sx, sy, ex, ey, ms = d.swipe(1050, 1820, 1220, 900)
        assert abs(sx - 1050) <= 6 and abs(ex - 1050) <= 12        # inside a 30 px margin strip
        assert abs(sy - 1820) <= 15 and 0.92 * 600 - 1 <= sy - ey <= 600   # never longer: dumps keep overlapping
        assert 765 <= ms <= 1080
        assert r.calls[0][-5:] == [str(sx), str(sy), str(ex), str(ey), str(ms)]
        seen.add((sx, sy, ex, ey, ms))
        sx, sy, ex, ey, ms = d.swipe_h(1000, 900, 180, 150)
        assert abs(sy - 1000) <= 8 and abs(ey - sy) <= 6 and abs(sx - 900) <= 15 and abs(ex - 180) <= 15
    assert len(seen) == 50


# --- the dumper's serve mode ---

class ServePhone:
    """The phone side of serve mode: each connect gets a socketpair end, a thread answers like Dump.kt.
    `answers` per request: an xml string → ok, "ERR …" → err line, None → hang up."""

    def __init__(self, answers, silent: bool = False):
        self.answers = list(answers)
        self.silent = silent          # an old dumper: nothing listens, adb closes the connection
        self.requests: list[str] = []
        self.spawned: list[list[str]] = []

    def spawn(self, cmd):
        self.spawned.append(cmd)
        return type("Proc", (), {"wait": lambda self, timeout=None: 0, "kill": lambda self: None})()

    def connect(self, port):
        ours, theirs = socket.socketpair()
        threading.Thread(target=self._serve, args=(theirs,), daemon=True).start()
        return ours

    def _serve(self, s):
        f = s.makefile("rb")
        with s:
            if self.silent:
                return
            while line := f.readline():
                self.requests.append(line.decode().strip())
                if line.strip() == b"quit":
                    return
                a = self.answers.pop(0) if self.answers else None
                if a is None:
                    return
                if a.startswith("ERR "):
                    s.sendall(f"err {a[4:]}\n".encode())
                else:
                    gz = gzip.compress(a.encode())
                    s.sendall(f"ok {len(gz)} 1\n".encode() + gz)


def adb_forward(cmd, stdin, timeout):
    if cmd[-2:] == ["forward", "--list"]:
        return 0, b"SER tcp:5555 localabstract:buddy.screener.dump\n", b""
    if "forward" in cmd:
        return 0, (b"40123\n" if cmd[-2] == "tcp:0" else b""), b""
    raise AssertionError(cmd)


def served_dev(phone, run=adb_forward) -> Device:
    calls = []

    def runner(cmd, stdin, timeout):
        calls.append(cmd)
        return run(cmd, stdin, timeout)
    served = _Served(["adb", "-s", "SER"], "d/.Dump", runner, spawn=phone.spawn, connect=phone.connect,
                     sleep=lambda s: None)
    d = Device("SER", run=runner, sleep=lambda s: None, dumper="d/.Dump", served=served)
    d.calls = calls
    return d


def test_serve_mode_one_start_for_many_reads():
    phone = ServePhone(["<a/>", "<b/>", "<c/>"])
    d = served_dev(phone)
    assert [d.dump(), d.dump(), d.dump()] == ["<a/>", "<b/>", "<c/>"]
    assert phone.spawned == [["adb", "-s", "SER", "shell", "am", "instrument", "-w", "-e", "serve", "1", "d/.Dump"]]
    assert phone.requests == ["dump 500"] * 3
    assert ["adb", "-s", "SER", "forward", "--remove", "tcp:5555"] in d.calls   # a killed caller's leftover
    d.close()
    assert d.calls[-1] == ["adb", "-s", "SER", "forward", "--remove", "tcp:40123"]


def test_serve_mode_error_answer_retried_on_the_same_connection():
    phone = ServePhone(["ERR IllegalStateException: no_window", "<a/>"])
    d = served_dev(phone)
    assert d.dump() == "<a/>" and len(phone.spawned) == 1


def test_serve_mode_restarts_after_its_link_broke():
    phone = ServePhone(["<a/>", None, "<b/>"])   # killed after one read (another reader force-stopped it)
    d = served_dev(phone)
    assert d.dump() == "<a/>"
    assert d.dump() == "<b/>" and len(phone.spawned) == 2


def test_dumper_without_serve_mode_falls_back_to_one_shot_reads(monkeypatch):
    monkeypatch.setattr(device_mod, "SERVE_START_S", 0)
    phone = ServePhone([], silent=True)

    def run(cmd, stdin, timeout):
        if "forward" in cmd:
            return adb_forward(cmd, stdin, timeout)
        return 0, own("<hierarchy/>"), b""
    d = served_dev(phone, run)
    assert d.dump() == "<hierarchy/>" and d.dump() == "<hierarchy/>"
    assert len(phone.spawned) == 1   # tried once, one-shot from then on


def test_raw_screencap_parsed_into_an_image():
    from PIL import Image
    px = bytes([10, 20, 30, 255]) * (4 * 2)
    for head in (b"", b"\x00\x00\x00\x00"):   # Android 9 builds write 12 or 16 header bytes
        raw = (4).to_bytes(4, "little") + (2).to_bytes(4, "little") + (1).to_bytes(4, "little") + head + px
        r = Runner((0, b"Physical size: 4x2\n", b""), (0, raw, b""), (0, raw, b""))
        d = dev(r)
        img = d.screencap()
        assert img.size == (4, 2) and img.mode == "RGB" and img.getpixel((3, 1)) == (10, 20, 30)
        assert d.screencap(half=True).size == (2, 1)
        assert r.calls[1][3:] == ["exec-out", "screencap"] and len(r.calls) == 3   # size asked once
    bad = Runner((0, b"Physical size: 4x2\n", b""), (0, b"\x89PNG" + px, b""))
    with pytest.raises(AdbError):
        dev(bad).screencap()
