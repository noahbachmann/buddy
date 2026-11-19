"""buddy-screener: the service and its offline commands (#66).

Offline (no MQTT; on the Pi with buddy.env: `deploy/run.sh screener --cmd …`):
    --cmd screen                  which screen the phone shows (one line, counts only)
    --cmd dump [name]             raw UI dump to tmpfs (for a fixture), plus the one line
    --cmd fixture <out> <xml>…    sanitize raw dumps into <out>/ (one mapping across them; host forbidden list)
    --cmd profile [--show]        capture the profile on screen (scrolls; --show prints the texts)
    --cmd back-out                leave an open comment sheet without sending (clear, leave the app, relaunch)
    --cmd walk                    step 1 check: feed → whole profile → a section's comment sheet (type, read back,
                                  back out without sending) → likes → matches → a chat → back
    --cmd templates               check templates.md, list the like comments
    --cmd shots [out dir]         the profile on screen as screenshots, 4 screens per JPEG page (default: tmpfs
                                  shots/; a remote session sends Dev these; taps nothing)
Shadow mode (step 2; only skip/send act, and only on Dev's label; details in shadow.py):
    --cmd shadow                  capture the profile on screen, decide, store and print it (row id)
    --cmd label <id> like|skip|unjudged [section comment] [--text …] [--note …] [--wrong] [--dealbreaker] [--sensitive]
    --cmd type <id>               type the labelled comment into the labelled section's panel
    --cmd skip <id>               carry out Dev's skip (or unjudged) label: skip that profile (the captured one only)
    --cmd send <id>               carry out Dev's like label: send the typed comment (upsell → plain like, never paid)
    --cmd report [--since <id>]   step 2's done-when numbers, missing comments, usage (one session: rows ≥ id)
Chat sim (step 4, #77; no phone, real chat calls with sentinel contacts, transcripts to tmpfs):
    --sim [--case NAME …]         the scripted cases (all, or the named ones) and the gate report
    --sim --personas N [--case all]  N persona chats (a model plays her), with or without the cases

Service (no flags): retained state, `stop|pause|resume` on buddy/screener/cmd, phone check. Step 1 acts on nothing
by itself; browsing and chats come in later steps.
"""

import argparse
import json
import queue
import sys
import time
from pathlib import Path

from buddy_shared import lifecycle, systemd, topics
from buddy_shared.mqtt import Mqtt

from . import screens, vocab
from .config import Config
from .decide import DecideConfig, DecideError, Decider
from .device import OUR_IME, AdbError, Device, Stopped
from .library import Library, LibraryError
from .navigator import Navigator, NeedsYou
from .profile import Capture
from .sanitize import read_forbidden, sanitize_many
from .store import Store

TEST_TEXT = "Test äöü ß – ok"   # typed into a comment field and read back, never sent


def _device(cfg: Config) -> Device:
    return Device(cfg.serial, guard=lambda: cfg.guard(lifecycle.running))


def _navigator(cfg: Config, dev: Device) -> Navigator:
    from .imaging import crop_hash, crop_jpeg
    return Navigator(dev, cfg.package, dump_dir=cfg.tmp_dir, hasher=crop_hash,
                     cropper=lambda png, rect: crop_jpeg(png, rect, cfg.photo_px))


def _decider(cfg: Config, store: Store) -> Decider:
    return Decider(DecideConfig(cfg.model, cfg.effort, cfg.decide_timeout_s, cfg.daily_usd, cfg.token_file,
                                cfg.limit_file, cfg.tmp_dir / "claude", cfg.text_first), store)


def _ensure_our_ime(dev: Device) -> None:
    if dev.ime_current() != OUR_IME:
        if OUR_IME not in dev.ime_list():
            raise NeedsYou("the screener keyboard isn't enabled on the phone")
        dev.ime_set(OUR_IME)


def _stock_ime(cfg: Config, dev: Device) -> str:
    return cfg.stock_ime or next((i for i in dev.ime_list() if i != OUR_IME), "")


def cmd_profile(nav: Navigator, show: bool) -> Capture:
    nav.go_tab(vocab.TAB_DISCOVER)
    t = time.monotonic()
    cap = nav.capture_profile()
    p = cap.profile
    print(f"profile: {len(p.sections)} sections, {len(p.details)} details, {time.monotonic() - t:.0f} s; "
          f"swipes (nominal→measured px): {' '.join(f'{a}→{b}' for a, b in cap.moves)}")
    for s in p.sections:
        extra = f" {s.text!r}" if show and s.text else ""
        print(f"  {s.ref:9} y {s.top}..{s.bottom} {'hash ' + s.hash if s.hash else ''}{extra}")
    if show:
        print(f"  name: {p.name}; details: {p.details}")
    return cap


def cmd_walk(cfg: Config, dev: Device, nav: Navigator) -> None:
    _ensure_our_ime(dev)
    print("feed:", screens.summary(nav.go_tab(vocab.TAB_DISCOVER)))
    cap = cmd_profile(nav, show=False)

    # a prompt if there is one (the usual target), else the first photo
    target = next((s for s in cap.profile.sections if s.kind == "prompt"), cap.profile.sections[0])
    s, sv = nav.scroll_to(cap, target.ref)
    print(f"scrolled to {target.ref}")
    nav.open_sheet(s, sv)
    nav.type_comment(TEST_TEXT)
    print(f"comment sheet: typed and read back {len(TEST_TEXT)} chars")
    _, same = nav.back_out(cap.profile.name)
    print(f"backed out: no panel, nothing sent; {'same' if same else 'another'} profile after the relaunch")

    s = nav.go_tab(vocab.TAB_LIKES)
    print("likes:", screens.summary(s))
    s = nav.go_tab(vocab.TAB_MATCHES)
    print("matches:", screens.summary(s))
    if screens.match_rows(s.root):
        c = nav.open_chat(0)
        chat = screens.chat_view(c.root)
        sides = {side: sum(m.side == side for m in chat.messages) for side in ("left", "right")}
        print(f"chat: {len(chat.messages)} messages ({sides['left']} theirs, {sides['right']} ours)"
              f"{', match origin shown' if chat.origin else ''}")
        print("back:", screens.summary(nav.leave_chat()))
    print("feed:", screens.summary(nav.go_tab(vocab.TAB_DISCOVER)))
    print("walk done")


def cmd_shots(nav: Navigator, out: Path) -> None:
    from .imaging import shot_pages
    nav.go_tab(vocab.TAB_DISCOVER)
    shots = nav.screenshots()
    out.mkdir(parents=True, exist_ok=True, mode=0o700)
    for old in out.glob("page-*.jpg"):
        old.unlink()   # pages of the profile before
    for i, page in enumerate(shot_pages(shots), 1):
        path = out / f"page-{i}.jpg"
        path.write_bytes(page)
        path.chmod(0o600)
        print(path)
    print(f"{len(shots)} screens")


def cmd_shadow(cfg: Config, args: argparse.Namespace) -> int:
    """Shadow mode commands: only skip/send act, each on Dev's label of that row."""
    from . import shadow

    lib = Library.load(cfg.config_dir)
    store = Store(cfg.db_path)
    if args.cmd == "templates":
        for c in lib.like_comments:
            print(f"{c.id:24} {c.lang} {len(c.text):3}  {c.hint}")
        print(f"{len(lib.like_comments)} like comments ok")
        return 0
    if args.cmd == "report":
        rows, usage = shadow.session_rows(store.shadow(), store.usage_rows, args.since)
        print("\n".join(shadow.report(rows, usage)))
        return 0
    if args.cmd == "label":
        if len(args.args) not in (2, 4):
            print("label <id> like|skip|unjudged [section comment]", file=sys.stderr)
            return 2
        row_id = int(args.args[0])
        rows = store.shadow(row_id)
        if not rows:
            print(f"no shadow row {row_id}", file=sys.stderr)
            return 2
        cap = shadow.load_capture(cfg.tmp_dir, row_id)
        section, comment = (" ".join(args.args[2].split()), args.args[3]) if len(args.args) == 4 else (None, None)
        try:
            dev_label = shadow.make_label(rows[0], args.args[1], section, comment, lib,
                                          [s.ref for s in cap.profile.sections] if cap else None, args.note,
                                          args.wrong, args.dealbreaker, args.sensitive, args.text)
        except shadow.LabelError as e:
            print(f"label: {e}", file=sys.stderr)
            return 2
        store.label(row_id, dev_label)
        print(f"#{row_id} labelled: {dev_label['decision']}"
              + (f" {dev_label['comment_on']} / {dev_label['comment']}" if dev_label["decision"] == "like" else ""))
        return 0

    dev = _device(cfg)
    nav = _navigator(cfg, dev)
    if args.cmd == "shadow":
        r = shadow.run_shadow(nav, _decider(cfg, store), lib, store, cfg.tmp_dir)
        print("\n".join(shadow.describe(r, lib)))
        return 0
    if args.cmd == "type":
        row_id = int(args.args[0])
        rows = store.shadow(row_id)
        cap = shadow.load_capture(cfg.tmp_dir, row_id)
        if not rows or cap is None:
            print(f"#{row_id}: no capture in tmpfs (only the newest shadow row's): run shadow again", file=sys.stderr)
            return 2
        _ensure_our_ime(dev)
        try:
            text = shadow.type_label(nav, rows[0], cap, lib)
        except shadow.LabelError as e:
            print(f"type: {e}", file=sys.stderr)
            return 2
        print(f"#{row_id}: {rows[0].dev['comment_on']} panel open, typed and read back {len(text)} chars. "
              f"Send with --cmd send {row_id} (or tap it, or --cmd back-out).")
        return 0
    if args.cmd in ("skip", "send"):
        row_id = int(args.args[0])
        rows = store.shadow(row_id)
        cap = shadow.load_capture(cfg.tmp_dir, row_id)
        if not rows or cap is None:
            print(f"#{row_id}: no capture in tmpfs (only the newest shadow row's)", file=sys.stderr)
            return 2
        try:
            acted, nxt = shadow.act_label(nav, rows[0], cap, lib, args.cmd)
        except shadow.LabelError as e:
            print(f"{args.cmd}: {e}", file=sys.stderr)
            return 2
        store.label(row_id, acted)
        print(f"#{row_id}: {'skipped' if args.cmd == 'skip' else 'like sent'}; next profile: {nxt}")
        return 0
    raise AssertionError(args.cmd)


def offline(cfg: Config, args: argparse.Namespace) -> int:
    if args.cmd in ("templates", "report", "label", "shadow", "type", "skip", "send"):
        try:
            return cmd_shadow(cfg, args)
        except LibraryError as e:
            print(f"library: {e}", file=sys.stderr)
            return 1
        except DecideError as e:
            print(f"decide: {e}", file=sys.stderr)
            return 6
        except NeedsYou as e:
            print(f"needs you: {e}", file=sys.stderr)
            return 3
        except Stopped as e:
            print(f"stopped: {e}", file=sys.stderr)
            return 4
        except AdbError as e:
            print(f"phone: {e}", file=sys.stderr)
            return 5

    if args.cmd == "fixture":
        out, *files = args.args
        paths = [Path(f) for f in files]
        outs = sanitize_many([p.read_text() for p in paths], cfg.package, read_forbidden(cfg.forbidden_file))
        for p, x in zip(paths, outs):
            (Path(out) / p.name).write_text(x)
        print(f"{len(outs)} fixture(s) in {out}")
        return 0

    dev = _device(cfg)
    nav = _navigator(cfg, dev)
    try:
        if args.cmd == "screen":
            print(screens.summary(nav.stable()))
        elif args.cmd == "dump":
            s = nav.stable()
            cfg.tmp_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
            path = cfg.tmp_dir / f"{args.args[0] if args.args else time.strftime('%H%M%S')}.xml"
            path.write_text(nav.xml)
            print(f"{screens.summary(s)} → {path}")
        elif args.cmd == "profile":
            cmd_profile(nav, args.show)
        elif args.cmd == "back-out":
            s = nav.expect(screens.Kind.SHEET)
            s, same = nav.back_out(screens.profile_view(s.root).name)
            print(f"{screens.summary(s)}; {'same' if same else 'another'} profile")
        elif args.cmd == "walk":
            cmd_walk(cfg, dev, nav)
        elif args.cmd == "shots":
            cmd_shots(nav, Path(args.args[0]) if args.args else cfg.tmp_dir / "shots")
        else:
            print(f"unknown --cmd {args.cmd}", file=sys.stderr)
            return 2
    except NeedsYou as e:
        print(f"needs you: {e}", file=sys.stderr)
        return 3
    except Stopped as e:
        print(f"stopped: {e}", file=sys.stderr)
        return 4
    except AdbError as e:
        print(f"phone: {e}", file=sys.stderr)
        return 5
    return 0


def service(cfg: Config) -> int:
    dev = _device(cfg)
    events: queue.Queue = queue.Queue()
    mqtt = Mqtt("screener")
    mqtt.subscribe(topics.screener_cmd, lambda _t, payload: events.put(payload))
    if not mqtt.start():
        return 0

    published = None
    reason = ""
    next_check = 0.0
    phone = "?"
    while lifecycle.running():
        systemd.notify_watchdog()
        if time.monotonic() >= next_check:
            phone = dev.state()
            if phone == "device":
                try:
                    phone = "ok" if dev.foreground() == cfg.package else "other_app"
                except AdbError:
                    phone = "offline"
            next_check = time.monotonic() + cfg.check_s
        state = {"state": {"active": "idle"}.get(cfg.mode(), cfg.mode()), "phone": phone}
        if reason:
            state["reason"] = reason
        if state != published:
            mqtt.publish(topics.screener_state, json.dumps(state), qos=1, retain=True)
            mqtt.log(f"state {state['state']}, phone {phone}")
            published = state
        try:
            payload = events.get(timeout=5)
        except queue.Empty:
            continue
        try:
            action = json.loads(payload).get("action", "")
        except (ValueError, AttributeError):
            action = ""
        reason = handle(cfg, dev, action) or ""
        next_check = 0.0
    published = {"state": "offline", "phone": phone}
    mqtt.publish(topics.screener_state, json.dumps(published), qos=1, retain=True)
    mqtt.stop()
    return 0


def handle(cfg: Config, dev: Device, action: str) -> str | None:
    """stop: the stop file (survives restarts); pause: Dev uses the phone (stock keyboard); resume: both off."""
    cfg.config_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    cfg.data_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    try:
        if action == "stop":
            cfg.stop_file.touch()
        elif action == "pause":
            cfg.pause_file.touch()
            if ime := _stock_ime(cfg, dev):
                dev.ime_set(ime)
        elif action == "resume":
            cfg.stop_file.unlink(missing_ok=True)
            cfg.pause_file.unlink(missing_ok=True)
            _ensure_our_ime(dev)
        else:
            return f"unknown action {action[:20]!r}"
    except (AdbError, NeedsYou) as e:
        return f"{action}: keyboard switch failed ({str(e)[:80]})"
    return None


def main() -> int:
    lifecycle.install_signal_handlers()
    p = argparse.ArgumentParser(prog="buddy-screener")
    p.add_argument("--cmd", help="screen | dump [name] | fixture <out> <xml…> | profile | back-out | walk | templates | shots [dir]"
                                 " | shadow | label <id> like|skip|unjudged [section comment] | type <id> | skip <id>"
                                 " | send <id> | report")
    p.add_argument("--show", action="store_true", help="profile: print texts (host terminal only)")
    p.add_argument("--since", type=int, help="report: only shadow rows from this id on (one session)")
    p.add_argument("--note", default="", help="label: Dev's note (why)")
    p.add_argument("--text", help="label: what goes out, Dev's version of the comment (default: the bot's adapted "
                                  "text for its own comment, else the template's)")
    p.add_argument("--wrong", action="store_true", help="label: the bot's comment reads wrong on its section")
    p.add_argument("--dealbreaker", action="store_true", help="label: Dev saw a dealbreaker")
    p.add_argument("--sensitive", action="store_true", help="label: the bot's reason infers something sensitive")
    p.add_argument("--sim", action="store_true", help="the chat flow against simulated matches (#77)")
    p.add_argument("--case", action="append", default=[], help="sim: only this scripted case (repeatable; all)")
    p.add_argument("--personas", type=int, default=0, help="sim: N persona chats (a model plays the match)")
    p.add_argument("args", nargs="*")
    args = p.parse_args()
    try:
        cfg = Config.from_env()
    except ValueError as e:
        print(f"[screener] config: {e}", file=sys.stderr)
        return 1
    if args.sim:
        from .sim import run_sim
        return run_sim(cfg, args.case, Store(cfg.db_path), args.personas)
    if args.cmd:
        return offline(cfg, args)
    return service(cfg)


if __name__ == "__main__":
    sys.exit(main())
