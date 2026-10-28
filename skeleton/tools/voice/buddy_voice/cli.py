"""buddy-voice: the PC side of voice training. Usually run through deploy/train.sh (docs/voice-training.md).

    train [options]          pull → check → fine-tune → convert → eval against the current model → install?
    pull                     sessions (and saved live commands) from the Pi → ~/buddy-voice
    check                    dataset report: levels, missing files, clips without speech
    eval <model>… [options]  score models on the test clips on the Pi
    install <model> [--ctx]  copy to the Pi, set BUDDY_STT_MODEL / BUDDY_STT_AUDIO_CTX, restart ears
    label                    go through saved live commands (BUDDY_EARS_SAVE_DIR) and keep them as training data
    import <dir> <name>      old record-voice.sh folder → session (repairs its numbering faults)
"""

import argparse
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

from . import data, pi


def cmd_pull(_args=None) -> None:
    data.SESSIONS.mkdir(parents=True, exist_ok=True)
    if pi.run("test -d ~/buddy/voice && echo yes", check=False).strip() == "yes":
        pi.rsync(f"{pi.HOST}:buddy/voice/", f"{data.SESSIONS}/", "--exclude", "prompts.tsv")
    live = pi.run(f"{pi.LOAD_ENV}; echo \"$BUDDY_EARS_SAVE_DIR\"").strip()
    if live:
        pi.rsync(f"{pi.HOST}:{live}/", f"{data.DATA / 'live'}/")
    names = data.session_names()
    print(f"✓ {len(names)} session(s) in {data.SESSIONS}" + (f", live commands from {live}" if live else ""))


def cmd_check(args) -> bool:
    from .vad import Vad  # noqa: PLC0415

    summary, issues = data.check(Vad(data.CACHE), args.sessions or None)
    print(f"{'session/source':<40} {'clips':>5} {'peak':>6} {'rms':>6} {'speech':>8}")
    for name, s in summary.items():
        print(f"{name:<40} {s['clips']:>5} {s['peak_db'] or 0:>6.1f} {s['rms_db'] or 0:>6.1f} {s['speech_s']:>7.0f}s")
    sets = Counter(c.set for c in data.clips(("train", "test", "wake"), args.sessions or None))
    print(f"clips: {sets['train']} train, {sets['test']} test, {sets['wake']} wake word")
    serious = [i for i in issues if not i.problem.startswith("sentence not in")]
    for i in issues if args.verbose else serious:
        print(f"  ⚠ {i.clip.id} ({i.source}): {i.problem} — \"{i.clip.sentence}\"")
    if len(issues) > len(serious) and not args.verbose:
        print(f"  ({len(issues) - len(serious)} sentences not in commands.tsv; -v lists them)")
    return not serious


def cmd_train(args) -> None:
    try:
        from .train import Config, run  # noqa: PLC0415
    except ImportError as e:
        raise SystemExit(f"{e}: PyTorch missing. deploy/train.sh installs it (uv --extra cuda)") from None
    from .evaluate import Variant, current_model, evaluate, install  # noqa: PLC0415

    if not args.no_pull:
        cmd_pull()
    if not cmd_check(args):
        print("⚠ fix or re-record the clips above (r in deploy/record.sh), or train anyway with --force")
        if not args.force:
            raise SystemExit(1)
    cfg = Config(base=args.base, source=args.source, ctx=args.ctx, epochs=args.epochs, lr=args.lr,
                 batch=args.batch, max_steps=args.max_steps, quantize=not args.no_quantize,
                 sessions=args.sessions or [])
    model = run(cfg)
    if args.no_eval:
        return
    ctx = "auto" if cfg.ctx == "auto" else "0"
    current, current_ctx = current_model().split()
    candidates = [model] + [p for p in [model.with_name(model.stem + "-q8_0.bin")] if p.exists()]
    variants = [Variant("current")] + [Variant(_upload(p), ctx=ctx) for p in candidates]
    print(f"\ncurrent on the Pi: {current} (ctx {current_ctx})")
    evaluate(variants, source=cfg.source if cfg.source != "both" else "raw", verbose=True)
    print("\nInstall on the Pi?")
    for n, p in enumerate(candidates, 1):
        print(f"  {n}) {p.name}")
    choice = input("number, Enter = keep the current model: ").strip()
    if choice.isdigit() and 1 <= int(choice) <= len(candidates):
        install(str(candidates[int(choice) - 1]), ctx)


def _upload(path: Path) -> str:
    from .evaluate import upload_model  # noqa: PLC0415

    return upload_model(str(path))


def cmd_eval(args) -> None:
    from .evaluate import Variant, evaluate, upload_model  # noqa: PLC0415

    variants = [Variant(upload_model(m), ctx=c, fallback=f)
                for m in args.models for c in args.ctx.split(",") for f in args.fallback.split(",")]
    evaluate(variants, source=args.source, sessions=args.sessions or None, prompt=args.prompt, verbose=args.verbose)


def cmd_install(args) -> None:
    from .evaluate import install  # noqa: PLC0415

    ctx = args.ctx
    sidecar = Path(args.model).expanduser().with_suffix(".json")
    if ctx is None and sidecar.exists():
        import json  # noqa: PLC0415

        ctx = "auto" if json.loads(sidecar.read_text())["ctx"] == "auto" else "0"
    install(args.model, ctx or "0")


def _player() -> list[str] | None:
    for cmd in (["paplay"], ["aplay", "-q"], ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet"]):
        if shutil.which(cmd[0]):
            return cmd
    return None


def cmd_label(args) -> None:
    live = data.DATA / "live"
    wavs = sorted(live.glob("*.wav"))
    if not wavs:
        raise SystemExit(f"no live commands in {live}: set BUDDY_EARS_SAVE_DIR on the Pi, use Buddy, then pull")
    player = _player()
    session = "live"
    existing = data.load_session(session) if (data.SESSIONS / session / "metadata.csv").exists() else []
    n = max([int(c.file[:5]) for c in existing], default=0)
    print("Enter = transcript is right, type the correct sentence, d = not a command (delete), s = later, q = quit")
    for wav in wavs:
        heard = wav.with_suffix(".txt").read_text().strip() if wav.with_suffix(".txt").exists() else ""
        if player:
            subprocess.run([*player, str(wav)], check=False)
        answer = input(f"\n  {wav.name}\n  heard: {heard}\n  > ").strip()
        if answer == "q":
            break
        if answer == "s":
            continue
        if answer != "d":
            n += 1
            file = f"{n:05d}.wav"
            dst = data.SESSIONS / session / args.source / file
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(wav, dst)
            existing.append(data.Clip(session, file, answer or heard, "train"))
            data.write_session(session, existing)
        wav.unlink()
        wav.with_suffix(".txt").unlink(missing_ok=True)
    print(f"✓ {len(existing)} live clips in session '{session}'. The Pi keeps its copies: delete them there when done.")


def cmd_import(args) -> None:
    for note in data.import_old(Path(args.dir).expanduser(), args.name, args.set):
        print("  repaired:", note)
    print(f"✓ session {args.name}: {len(data.load_session(args.name))} clips")


def main() -> int:
    p = argparse.ArgumentParser(prog="buddy-voice", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def sessions(sp):
        sp.add_argument("--sessions", nargs="*", help="only these sessions (default: all)")

    sub.add_parser("pull").set_defaults(fn=cmd_pull)
    sp = sub.add_parser("check")
    sessions(sp)
    sp.add_argument("-v", "--verbose", action="store_true")
    sp.set_defaults(fn=cmd_check)

    sp = sub.add_parser("train")
    sessions(sp)
    sp.add_argument("--base", default="base.en", help="whisper size: tiny.en, base.en, small.en (default base.en)")
    # aec: only the sessions recorded before 2026-10-28's switch to the raw mic (D13) have it
    sp.add_argument("--source", default="raw", choices=["raw", "aec", "both"])
    sp.add_argument("--ctx", default="auto", choices=["auto", "full"], help="audio context to train for")
    sp.add_argument("--epochs", type=int, default=10)
    sp.add_argument("--lr", type=float, default=1e-5)
    sp.add_argument("--batch", type=int, default=16)
    sp.add_argument("--max-steps", type=int, default=0, help="stop early (smoke test)")
    sp.add_argument("--no-quantize", action="store_true")
    sp.add_argument("--no-pull", action="store_true")
    sp.add_argument("--no-eval", action="store_true")
    sp.add_argument("--force", action="store_true", help="train despite check warnings")
    sp.add_argument("-v", "--verbose", action="store_true")
    sp.set_defaults(fn=cmd_train)

    sp = sub.add_parser("eval")
    sessions(sp)
    sp.add_argument("models", nargs="+", help="file names in the Pi's ~/buddy/models, local .bin paths, or 'current'")
    sp.add_argument("--ctx", default="", help="comma list of BUDDY_STT_AUDIO_CTX values, e.g. 0,auto")
    sp.add_argument("--fallback", default="", help="comma list of BUDDY_STT_FALLBACK values, e.g. 1,0")
    sp.add_argument("--source", default="raw", choices=["raw", "aec"])
    sp.add_argument("--prompt", default=None, help="BUDDY_STT_PROMPT for this run ('' = none)")
    sp.add_argument("-v", "--verbose", action="store_true", help="list the clips with a wrong intent")
    sp.set_defaults(fn=cmd_eval)

    sp = sub.add_parser("install")
    sp.add_argument("model")
    sp.add_argument("--ctx", default=None, help="BUDDY_STT_AUDIO_CTX (default: what the model was trained for)")
    sp.set_defaults(fn=cmd_install)

    sp = sub.add_parser("label")
    sp.add_argument("--source", default="raw", choices=["raw", "aec"], help="what BUDDY_EARS_DEVICE records")
    sp.set_defaults(fn=cmd_label)

    sp = sub.add_parser("import")
    sp.add_argument("dir")
    sp.add_argument("name", help="session name, e.g. 2026-10-24-old-train")
    sp.add_argument("--set", default="train", choices=["train", "test"])
    sp.set_defaults(fn=cmd_import)

    args = p.parse_args()
    try:
        result = args.fn(args)
    except KeyboardInterrupt:
        return 130
    return 1 if result is False else 0


if __name__ == "__main__":
    sys.exit(main())
