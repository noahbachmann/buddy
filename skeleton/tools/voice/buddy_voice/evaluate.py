"""Score speech-to-text models on the test clips, on the Pi, with the real whisper.cpp build (buddy-ears --transcribe).

Scores per variant (model × audio_ctx × fallback):
    exact   transcript equals the sentence after normalisation (score.norm)
    intent  brain gets the right action, query and speaker from it: what actually matters. Counted over clips whose
            sentence brain understands (lights etc. have no skill yet)
    WER     word error rate
    ms      speech-to-text time per clip on the Pi (avg / 90th percentile)
"""

import json
import shlex
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from . import corpus, data, pi
from .score import norm, word_errors
from .vad import Vad

REMOTE_DIR = "/tmp/buddy-eval"  # tmpfs on the Pi: nothing touches the microSD


@dataclass(frozen=True)
class Variant:
    model: str        # file name in ~/buddy/models, or "current" = exactly what buddy.env says
    ctx: str = ""     # BUDDY_STT_AUDIO_CTX: "0", "auto", N; "" = buddy.env's
    fallback: str = ""  # BUDDY_STT_FALLBACK: "1" / "0"; "" = buddy.env's

    def label(self) -> str:
        parts = [self.model] + ([f"ctx={self.ctx}"] if self.ctx else []) + ([f"fb={self.fallback}"] if self.fallback else [])
        return " ".join(parts)

    def env(self) -> str:
        out = []
        if self.model != "current":
            out.append(f"BUDDY_STT_MODEL={shlex.quote(self.model)}")
        if self.ctx:
            out.append(f"BUDDY_STT_AUDIO_CTX={shlex.quote(self.ctx)}")
        if self.fallback:
            out.append(f"BUDDY_STT_FALLBACK={shlex.quote(self.fallback)}")
        return " ".join(out)


def upload_model(model: str) -> str:
    """A local path is copied to the Pi's models folder (if not there yet); returns the name ears loads."""
    path = Path(model).expanduser()
    if not path.is_file():
        return model
    remote = f"buddy/models/{path.name}"
    if pi.run(f"test -f ~/{remote} && echo yes", check=False).strip() != "yes":
        print(f"  ↑ {path.name} → Pi", flush=True)
        subprocess.run(["scp", "-q", str(path), f"{pi.HOST}:{remote}"], check=True)
    return path.name


def current_model() -> str:
    out = pi.run(f"{pi.LOAD_ENV}; echo \"${{BUDDY_STT_MODEL:-ggml-base.en.bin}} ${{BUDDY_STT_AUDIO_CTX:-0}}\"")
    return out.strip()


def evaluate(variants: list[Variant], source: str = "aec", sessions: list[str] | None = None,
             prompt: str | None = None, verbose: bool = False) -> list[dict]:
    vad = Vad(data.CACHE)
    known = corpus.by_sentence(corpus.load())
    tests = []
    for clip in data.clips(("test",), sessions):
        path = data.trimmed(clip, source, vad)
        if path is not None:
            tests.append((clip, path))
    if not tests:
        raise SystemExit(f"no test clips ({source}): record a test session (deploy/record.sh <label> test)")

    # Trimmed clips → Pi, same relative paths as under trimmed/
    trimmed_root = data.DATA / "trimmed"
    pi.run(f"rm -rf {REMOTE_DIR} && mkdir -p {REMOTE_DIR}")
    rels = [str(p.relative_to(trimmed_root)) for _, p in tests]
    subprocess.run(["rsync", "-a", "--files-from=-", str(trimmed_root), f"{pi.HOST}:{REMOTE_DIR}/"],
                   input="\n".join(rels), text=True, check=True)
    print(f"{len(tests)} test clips ({source}) from {len({c.session for c, _ in tests})} session(s)\n", flush=True)

    heard_by_variant = {}
    prompt_env = f"BUDDY_STT_PROMPT={shlex.quote(prompt)}" if prompt is not None else ""
    for v in variants:
        cmd = (f"{pi.LOAD_ENV}; cd {REMOTE_DIR} && {v.env()} {prompt_env} ~/buddy/bin/buddy-ears --transcribe "
               + " ".join(shlex.quote(r) for r in rels) + " 2>/dev/null")
        out = {}
        for line in pi.run(cmd).splitlines():
            file, ms, *text = line.split("\t")
            out[file] = (int(ms), text[0] if text else "")
        if not out:
            print(f"  ✗ {v.label()}: no output (model missing in ~/buddy/models?)")
        heard_by_variant[v] = out

    texts = {c.sentence for c, _ in tests} | {t for out in heard_by_variant.values() for _, t in out.values()}
    intents = pi.brain(sorted(texts))

    def expected(sentence: str) -> tuple[str, str]:
        c = known.get(sentence)
        return (c.intent, c.query, c.device) if c else intents.get(sentence, ("unknown", "", ""))

    results = []
    for v, out in heard_by_variant.items():
        if not out:
            continue
        rows = []
        for (clip, _), rel in zip(tests, rels):
            ms, heard = out.get(rel, (0, ""))
            want = expected(clip.sentence)
            errs, words = word_errors(clip.sentence, heard)
            rows.append({"clip": clip.id, "session": clip.session, "said": clip.sentence, "heard": heard, "ms": ms,
                         "exact": norm(heard) == norm(clip.sentence), "errors": errs, "words": words,
                         "music": want[0] != "unknown", "intent_ok": intents.get(heard, ("unknown", "", "")) == want})
        results.append({"variant": v.label(), **summarize(rows), "sessions": {
            s: summarize([r for r in rows if r["session"] == s]) for s in sorted({r["session"] for r in rows})},
            "rows": rows})

    print_table(results)
    if verbose:
        for r in results:
            print(f"\n== {r['variant']}: wrong intent")
            for row in r["rows"]:
                if row["music"] and not row["intent_ok"]:
                    print(f"  {row['clip']}\n      said: {row['said']}\n     heard: {row['heard']}")
    report = data.DATA / "evals" / f"{time.strftime('%Y%m%d-%H%M%S')}.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({"source": source, "prompt": prompt, "results": results}, indent=2))
    print(f"\nreport: {report}")
    return results


def summarize(rows: list[dict]) -> dict:
    music = [r for r in rows if r["music"]]
    ms = sorted(r["ms"] for r in rows)
    return {
        "clips": len(rows),
        "exact": sum(r["exact"] for r in rows),
        "intent": sum(r["intent_ok"] for r in music),
        "music": len(music),
        "wer": sum(r["errors"] for r in rows) / max(1, sum(r["words"] for r in rows)),
        "ms_avg": round(statistics.mean(ms)) if ms else 0,
        "ms_p90": ms[int(0.9 * (len(ms) - 1))] if ms else 0,
    }


def print_table(results: list[dict]) -> None:
    width = max(len(r["variant"]) for r in results)
    print(f"{'variant':<{width}}  {'exact':>9}  {'intent':>9}  {'WER':>6}  {'ms avg/p90':>11}")
    for r in results:
        print(f"{r['variant']:<{width}}  {r['exact']:>4} /{r['clips']:>3}  {r['intent']:>4} /{r['music']:>3}  "
              f"{r['wer']:>6.1%}  {r['ms_avg']:>5}/{r['ms_p90']:<5}")
        if len(r["sessions"]) > 1:
            for s, x in r["sessions"].items():
                print(f"  {s:<{width - 2}}  {x['exact']:>4} /{x['clips']:>3}  {x['intent']:>4} /{x['music']:>3}  "
                      f"{x['wer']:>6.1%}")


def install(model: str, ctx: str) -> None:
    """Copies the model to the Pi (if local), sets BUDDY_STT_MODEL and BUDDY_STT_AUDIO_CTX, restarts ears."""
    name = upload_model(model)
    env = "~/buddy/config/buddy.env"
    for key, value in (("BUDDY_STT_MODEL", name), ("BUDDY_STT_AUDIO_CTX", ctx)):
        pi.run(f"grep -q '^{key}=' {env} && sed -i 's|^{key}=.*|{key}={value}|' {env} || echo '{key}={value}' >> {env}")
    pi.run("XDG_RUNTIME_DIR=/run/user/$(id -u) systemctl --user restart buddy@ears")
    time.sleep(3)
    print(pi.run("journalctl --user-unit buddy@ears -n 20 --no-pager -o cat | grep -E 'loaded|capturing' | tail -2"))
