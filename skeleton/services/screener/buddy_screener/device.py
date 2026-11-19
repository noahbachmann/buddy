"""The phone over adb: read the screen, act on it, drive the screener keyboard.

Every input action (tap, swipe, key, text, launch) calls `guard()` first: the kill switch (stop file, pause,
shutdown) raises `Stopped` there, so no action can slip out after a stop. Reads don't check it.
"""

import atexit
import base64
import gzip
import random
import re
import socket
import subprocess
import time
from collections.abc import Callable
from dataclasses import dataclass

from .uitree import Rect

KEY_BACK = 4
IDLE_ERROR = "could not get idle state"
IDLE_TRIES = 2   # a blinking cursor passes on the 2nd try; a playing video never does
OUR_IME = "buddy.screener.ime/.ScreenerIme"
OUR_DUMPER = "buddy.screener.dump/.Dump"   # reads without waiting for idle (android/screener-dump)
SERVE_SOCKET = "buddy.screener.dump"   # the dumper's serve mode listens here (abstract)
SERVE_IDLE_MS = 500    # quiet this long = settled (the dumper's one-shot default)
SERVE_START_S = 8.0    # first answer of a fresh serve mode; an old APK without it never answers
_DUMPER_MISSING = ("Unable to find instrumentation", "INSTRUMENTATION_FAILED")
_DUMP_XML = re.compile(r"INSTRUMENTATION_RESULT: xml=(\S+)")
_IME_PKG = OUR_IME.split("/", 1)[0]


class AdbError(RuntimeError):
    """adb failed or the phone is gone (unplugged = stop, one of the kill switch layers)."""


class NotIdle(AdbError):
    """uiautomator never saw the screen idle: something on it keeps moving (an autoplaying video). Scrolling it off
    screen helps, waiting doesn't. Only from the fallback reader: our own doesn't wait for idle."""


class Stopped(RuntimeError):
    """The kill switch is on: no more input actions."""


class _ServeDown(AdbError):
    """The serve mode's link broke (killed, force-stopped by another reader, phone gone)."""


class _ServeReply(AdbError):
    """The serve mode answered with an error (e.g. no_window right after a screen change): it still runs."""


class _Served:
    """The dumper's serve mode (`-e serve 1`): one UiAutomation connection for every read, through `adb forward` to
    its local socket. Started lazily, ended by `close()` (also at exit; the dumper ends itself 120 s after its last
    request in any case)."""

    def __init__(self, base: list[str], dumper: str, run: "Runner",
                 spawn: Callable[[list[str]], object] | None = None,
                 connect: Callable[[int], socket.socket] | None = None, sleep: Callable[[float], object] = time.sleep):
        self._base, self._dumper, self._run, self._sleep = base, dumper, run, sleep
        self._spawn = spawn or (lambda cmd: subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                                             stderr=subprocess.DEVNULL))
        self._connect = connect or (lambda port: socket.create_connection(("127.0.0.1", port), timeout=30))
        self._proc = None
        self._port = 0
        self._sock: socket.socket | None = None
        self._file = None
        self._head = b""   # the start's first answer line, taken by the next `_take`

    @property
    def running(self) -> bool:
        return self._sock is not None

    def read(self, idle_ms: int = SERVE_IDLE_MS) -> str:
        if self._sock is None:
            self._start(idle_ms)
            return self._take()
        try:
            self._sock.sendall(f"dump {idle_ms}\n".encode())
        except OSError as e:
            raise _ServeDown(f"serve mode: {e}") from None
        return self._take()

    def _forward(self, *args: str) -> str:
        code, out, err = self._run(self._base + ["forward", *args], None, 10)
        if code != 0:
            raise AdbError(f"adb forward failed: {err.decode(errors='replace').strip()[:120]}")
        return out.decode(errors="replace")

    def _start(self, idle_ms: int) -> None:
        """Spawns the serve mode and connects. adb accepts a forwarded connection before anything listens on the
        phone and then closes it: retried until the first request is answered."""
        for line in self._forward("--list").splitlines():   # left over by a killed caller
            if line.endswith(f"localabstract:{SERVE_SOCKET}"):
                self._forward("--remove", line.split()[1])
        self._proc = self._spawn(self._base + ["shell", "am", "instrument", "-w", "-e", "serve", "1", self._dumper])
        atexit.register(self.close)
        try:
            self._port = int(self._forward("tcp:0", f"localabstract:{SERVE_SOCKET}").strip())
        except ValueError:
            self.close()
            raise _ServeDown("adb forward gave no port") from None
        end = time.monotonic() + SERVE_START_S
        while True:
            try:
                sock = self._connect(self._port)
                sock.sendall(f"dump {idle_ms}\n".encode())
                f = sock.makefile("rb")
                head = f.readline()
                if head:
                    self._sock, self._file, self._head = sock, f, head
                    return
                sock.close()
            except OSError:
                pass
            if time.monotonic() >= end:
                self.close()
                raise _ServeDown("serve mode didn't start (dumper without it?)")
            self._sleep(0.1)

    def _take(self) -> str:
        assert self._file is not None
        try:
            head, self._head = (self._head or self._file.readline()), b""
            parts = head.decode(errors="replace").split(" ", 2)
            if parts[0] == "err":
                raise _ServeReply(f"dumper failed: {head.decode(errors='replace').strip()[4:][:120]}")
            if parts[0] != "ok" or len(parts) < 2:
                raise _ServeDown(f"serve mode: bad answer {head[:40]!r}")
            data = self._file.read(int(parts[1]))
            if len(data) != int(parts[1]):
                raise _ServeDown("serve mode: answer cut off")
        except (OSError, ValueError) as e:
            raise _ServeDown(f"serve mode: {e}") from None
        return gzip.decompress(data).decode()

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.sendall(b"quit\n")
            except OSError:
                pass
            self._sock.close()
            self._sock = self._file = None
        if self._proc is not None:
            try:
                self._proc.wait(timeout=3)   # quit ends the instrumentation and with it the adb shell
            except Exception:
                self._proc.kill()
            self._proc = None
        if self._port:
            try:
                self._forward("--remove", f"tcp:{self._port}")
            except AdbError:
                pass
            self._port = 0
        atexit.unregister(self.close)


@dataclass
class ImeResult:
    ok: bool
    text: str      # the field's full text after the command (ok) or the error code (no_field, …)


Runner = Callable[[list[str], bytes | None, float], tuple[int, bytes, bytes]]


def _run(cmd: list[str], stdin: bytes | None, timeout: float) -> tuple[int, bytes, bytes]:
    try:
        p = subprocess.run(cmd, input=stdin, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise AdbError(f"timeout: {' '.join(cmd[:4])}") from None
    except FileNotFoundError:
        raise AdbError("adb not installed") from None
    return p.returncode, p.stdout, p.stderr


class Device:
    def __init__(self, serial: str = "", guard: Callable[[], None] = lambda: None, run: Runner = _run,
                 rng: random.Random | None = None, sleep: Callable[[float], object] = time.sleep,
                 dumper: str = OUR_DUMPER, serve: bool = True, served: _Served | None = None):
        self._base = ["adb"] + (["-s", serial] if serial else [])
        self.dumper = dumper   # "" = uiautomator only (also after the dumper turned out missing)
        # the dumper's serve mode first; off for good once a fresh start never answers
        self._served = served or (_Served(self._base, dumper, run, sleep=sleep) if serve and dumper else None)
        self.guard = guard
        self._run = run
        self._rng = rng or random.Random()
        self._sleep = sleep

    # --- plumbing ---

    def adb(self, *args: str, timeout: float = 20) -> bytes:
        code, out, err = self._run(self._base + list(args), None, timeout)
        if code != 0:
            raise AdbError(f"adb {args[0]} failed ({code}): {err.decode(errors='replace').strip()[:200]}")
        return out

    def shell(self, *args: str, timeout: float = 20) -> str:
        return self.adb("shell", *args, timeout=timeout).decode(errors="replace").replace("\r", "")

    def state(self) -> str:
        """"device" when usable; "offline", "unauthorized", "missing" otherwise. Never raises."""
        try:
            code, out, err = self._run(self._base + ["get-state"], None, 10)
        except AdbError:
            return "missing"
        if code == 0:
            return out.decode().strip()
        msg = err.decode(errors="replace")
        return "unauthorized" if "unauthorized" in msg else "offline" if "offline" in msg else "missing"

    # --- reading ---

    def dump(self, tries: int = 5) -> str:
        """The UI tree as XML, in `uiautomator dump`'s format. Read by our dumper (no idle wait: a playing video
        can't block it), else by uiautomator, which fails now and then ("could not get idle state", e.g. a blinking
        cursor): retried with a short pause. Idle failures give up after IDLE_TRIES (each one already waited ~10 s)
        → `NotIdle`."""
        if self._served is not None:
            xml = self._dump_served(tries)
            if xml is not None:
                return xml
        if self.dumper:
            xml = self._dump_own(tries)
            if xml is not None:
                return xml
        last, idle = "", 0
        for i in range(tries):
            out = self.adb("exec-out", "uiautomator", "dump", "/dev/tty", timeout=30).decode(errors="replace")
            end = out.rfind("</hierarchy>")
            if end >= 0:
                return out[: end + len("</hierarchy>")]   # drop "UI hierchary dumped to: /dev/tty"
            last = out.strip()[-120:]
            if IDLE_ERROR in out:
                idle += 1
                if idle == IDLE_TRIES:
                    raise NotIdle(f"uiautomator dump failed: {last}")
            self._sleep(0.5 + 0.5 * i)
        raise (NotIdle if IDLE_ERROR in last else AdbError)(f"uiautomator dump failed: {last}")

    def _dump_served(self, tries: int) -> str | None:
        """None = serve mode unusable: one-shot reads from now on. A link that broke after it worked restarts."""
        assert self._served is not None
        last = ""
        for i in range(tries):
            was_running = self._served.running
            try:
                return self._served.read()
            except _ServeReply as e:
                last = str(e)
                self._sleep(0.5 + 0.5 * i)
            except _ServeDown as e:
                self._served.close()
                if not was_running:
                    self._served = None   # a fresh start never answered
                    return None
                last = str(e)
        raise AdbError(last or "dumper failed")

    def close(self) -> None:
        """Ends the serve mode (frees the phone's one UiAutomation connection)."""
        if self._served is not None:
            self._served.close()

    def _dump_own(self, tries: int) -> str | None:
        """None = the dumper isn't installed (then uiautomator from now on)."""
        last = ""
        for i in range(tries):
            out = self.shell("am", "instrument", "-w", self.dumper, timeout=30)
            if m := _DUMP_XML.search(out):
                return gzip.decompress(base64.b64decode(m.group(1))).decode()
            if any(x in out for x in _DUMPER_MISSING):
                self.dumper = ""
                return None
            last = out.strip()[-120:]
            self._sleep(0.5 + 0.5 * i)
        raise AdbError(f"dumper failed: {last}")

    def size(self) -> tuple[int, int]:
        """Screen width and height in px (an override wins over the physical size)."""
        out = self.shell("wm", "size")
        sizes = re.findall(r"(\w+) size: (\d+)x(\d+)", out)
        if not sizes:
            raise AdbError(f"wm size: {out.strip()[:80]}")
        _, w, h = next((x for x in sizes if x[0] == "Override"), sizes[0])
        return int(w), int(h)

    def screencap(self) -> bytes:
        """PNG of the whole screen."""
        return self.adb("exec-out", "screencap", "-p", timeout=30)

    def foreground(self) -> str:
        """Package of the focused window ("" if none)."""
        m = re.search(r"mCurrentFocus=Window\{\S+ \S+ ([^/ }]+)", self.shell("dumpsys", "window", "windows"))
        return m.group(1) if m else ""

    def app_version(self, package: str) -> str:
        m = re.search(r"versionName=(\S+)", self.shell("dumpsys", "package", package))
        return m.group(1) if m else ""

    def notifications(self, package: str) -> list[dict[str, str]]:
        """Posted notifications of one app: [{"title", "text"}]. ⚠️ The app's own format is unseen (step 0):
        parsed from the generic extras lines."""
        out = self.shell("dumpsys", "notification", "--noredact")
        items = []
        for block in re.split(r"\n\s+NotificationRecord\(", out)[1:]:
            if f"pkg={package}" not in block.split("\n", 1)[0]:
                continue
            title = re.search(r"android\.title=\S+ \((.*)\)", block)
            text = re.search(r"android\.text=\S+ \((.*)\)", block)
            items.append({"title": title.group(1) if title else "", "text": text.group(1) if text else ""})
        return items

    # --- input (guarded) ---

    def tap(self, target: Rect) -> tuple[int, int]:
        """A point near the target's centre (jitter within its middle half), never on its edge."""
        self.guard()
        cx, cy = target.center
        x = cx + round(self._rng.uniform(-0.25, 0.25) * target.width)
        y = cy + round(self._rng.uniform(-0.25, 0.25) * target.height)
        self.shell("input", "tap", str(x), str(y))
        return x, y

    def swipe(self, x: int, y1: int, y2: int, ms: int) -> tuple[int, int, int, int, int]:
        """Slow swipes scroll by about their length; fast ones fling. Never the same twice, like a finger: start
        jittered, a slight sideways drift, speed ±, length only ever shorter (callers' maxima keep dumps
        overlapping; scroll distances are measured, not assumed). x stays within ±12 (a 30 px margin strip)."""
        self.guard()
        r = self._rng
        sx = x + round(r.uniform(-6, 6))
        ex = sx + round(r.uniform(-6, 6))
        sy = y1 + round(r.uniform(-15, 15))
        ey = sy + round((y2 - y1) * r.uniform(0.92, 1.0))
        ms = round(ms * r.uniform(0.85, 1.2))
        self.shell("input", "swipe", str(sx), str(sy), str(ex), str(ey), str(ms))
        return sx, sy, ex, ey, ms

    def swipe_h(self, y: int, x1: int, x2: int, ms: int) -> tuple[int, int, int, int, int]:
        """Sideways, jittered like `swipe` (rows are explored until they stop moving: length needn't be exact)."""
        self.guard()
        r = self._rng
        sy = y + round(r.uniform(-8, 8))
        ey = sy + round(r.uniform(-6, 6))
        sx, ex = x1 + round(r.uniform(-15, 15)), x2 + round(r.uniform(-15, 15))
        ms = round(ms * r.uniform(0.85, 1.2))
        self.shell("input", "swipe", str(sx), str(sy), str(ex), str(ey), str(ms))
        return sx, sy, ex, ey, ms

    def key(self, code: int) -> None:
        self.guard()
        self.shell("input", "keyevent", str(code))

    def launch(self, package: str) -> None:
        self.guard()
        activity = self.shell("cmd", "package", "resolve-activity", "--brief", package).strip().splitlines()
        if not activity or "/" not in activity[-1]:
            raise AdbError("app not installed or has no launcher activity")
        self.shell("am", "start", "-n", activity[-1])

    # --- keyboard ---

    def ime_current(self) -> str:
        return self.shell("settings", "get", "secure", "default_input_method").strip()

    def ime_list(self) -> list[str]:
        return [line.strip() for line in self.shell("ime", "list", "-s").splitlines() if line.strip()]

    def ime_set(self, ime: str) -> None:
        """Not guarded: `pause` switches to the stock keyboard while the kill switch is already on."""
        self.shell("ime", "set", ime)

    def _ime(self, action: str, *extra: str) -> ImeResult:
        out = self.shell("am", "broadcast", "-p", _IME_PKG, "-a", f"{_IME_PKG}.{action}", *extra)
        m = re.search(r'Broadcast completed: result=(-?\d+)(?:, data="(.*)")?', out)
        if not m:
            raise AdbError("keyboard didn't answer (not installed or not the active keyboard?)")
        data = m.group(2) or ""
        if m.group(1) != "1":
            return ImeResult(False, data or "no_answer")
        return ImeResult(True, base64.b64decode(data).decode())

    def ime_type(self, text: str) -> ImeResult:
        self.guard()
        return self._ime("TYPE", "--es", "b64", base64.b64encode(text.encode()).decode())

    def ime_clear(self) -> ImeResult:
        self.guard()
        return self._ime("CLEAR")

    def ime_read(self) -> ImeResult:
        """Answers ok/empty even without a focused field: only a read-back after a TYPE proves focus."""
        return self._ime("READ")
