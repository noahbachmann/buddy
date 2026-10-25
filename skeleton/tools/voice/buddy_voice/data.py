"""The voice dataset on the PC: ~/buddy-voice (BUDDY_VOICE_DIR). Never in Git (D16): it's your voice.

    sessions/<date>-<label>/      one recording session (deploy/record.sh), pulled from the Pi
        aec/NNNNN.wav             echo-cancelled mic (what ears hears with BUDDY_EARS_DEVICE=…buddy_aec_source)
        raw/NNNNN.wav             raw Jabra mic, recorded at the same time (missing in imported old data)
        metadata.csv              file,sentence,set
    trimmed/<session>/<source>/   clips cut to the speech like live commands (derived: safe to delete)
    runs/<time>-<base>/           training runs: model, GGML files, report.json
    evals/<time>.json             eval reports
    cache/                        downloads (Silero VAD, whisper.cpp converter)
"""

import csv
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import corpus
from .audio import SAMPLE_RATE, peak_db, read_wav, rms_db, write_wav
from .vad import Vad

DATA = Path(os.environ.get("BUDDY_VOICE_DIR", "~/buddy-voice")).expanduser()
SESSIONS = DATA / "sessions"
CACHE = DATA / "cache"
SOURCES = ("aec", "raw")


@dataclass(frozen=True)
class Clip:
    session: str
    file: str  # NNNNN.wav, the same name under aec/ and raw/
    sentence: str
    set: str  # train | test | wake

    def path(self, source: str) -> Path:
        return SESSIONS / self.session / source / self.file

    @property
    def id(self) -> str:
        return f"{self.session}/{self.file}"


def session_names() -> list[str]:
    return sorted(p.parent.name for p in SESSIONS.glob("*/metadata.csv"))


def load_session(name: str) -> list[Clip]:
    with open(SESSIONS / name / "metadata.csv", newline="") as f:
        return [Clip(name, r["file"], r["sentence"], r["set"]) for r in csv.DictReader(f)]


def write_session(name: str, clips: list[Clip]) -> None:
    path = SESSIONS / name / "metadata.csv"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "sentence", "set"])
        w.writerows([c.file, c.sentence, c.set] for c in clips)


def clips(sets: tuple[str, ...] = ("train", "test"), sessions: list[str] | None = None) -> list[Clip]:
    return [c for s in (sessions or session_names()) for c in load_session(s) if c.set in sets]


def import_old(src: Path, name: str, set_: str) -> list[str]:
    """Imports a folder from the old record-voice.sh (clips/ + metadata.csv with file_name,transcription) as an
    aec-only session. Repairs its two known faults and returns what was repaired:
    - a number listed twice: record-voice.sh numbered by file count, so a later run overwrote an existing clip;
      the file holds the later recording, so the later row wins
    - a row whose file is missing (deleted by hand)
    """
    with open(src / "metadata.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    by_file: dict[str, str] = {}
    notes = []
    for r in rows:
        file = Path(r["file_name"]).name
        if file in by_file:
            notes.append(f"{file}: listed twice, kept \"{r['transcription']}\" (dropped \"{by_file[file]}\")")
        by_file[file] = r["transcription"]
    out = []
    for file, sentence in sorted(by_file.items()):
        if not (src / "clips" / file).exists():
            notes.append(f"{file}: file missing, dropped \"{sentence}\"")
            continue
        dst = SESSIONS / name / "aec" / file
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / "clips" / file, dst)
        out.append(Clip(name, file, sentence, set_))
    write_session(name, out)
    return notes


def trimmed(clip: Clip, source: str, vad: Vad) -> Path | None:
    """The clip cut to its speech (cached). None if the file is missing or has no speech."""
    dst = DATA / "trimmed" / clip.session / source / clip.file
    marker = dst.with_suffix(".nospeech")
    if dst.exists():
        return dst
    if marker.exists() or not clip.path(source).exists():
        return None
    cut = vad.trim(read_wav(clip.path(source)))
    if cut is None:
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.touch()
        return None
    write_wav(dst, cut)
    return dst


@dataclass
class Issue:
    clip: Clip
    source: str
    problem: str


QUIET_DB = -30.0  # peak below this: too far away or too soft for a reliable clip
CLIP_DB = -0.5    # peak above this: clipped


def check(vad: Vad, sessions: list[str] | None = None) -> tuple[dict[str, dict], list[Issue]]:
    """Per session/source: clip count, median levels, speech seconds. Issues: missing files, unknown sentences,
    too quiet, clipped, no speech. Also fills the trimmed/ cache."""
    known = corpus.by_sentence(corpus.load())
    summary, issues = {}, []
    for name in sessions or session_names():
        all_clips = load_session(name)
        for clip in all_clips:
            if clip.set != "wake" and clip.sentence not in known:
                issues.append(Issue(clip, "-", "sentence not in commands.tsv (intent scoring falls back to brain)"))
        for source in SOURCES:
            if not (SESSIONS / name / source).is_dir():
                continue
            peaks, rmss, speech = [], [], 0.0
            for clip in all_clips:
                if not clip.path(source).exists():
                    issues.append(Issue(clip, source, "file missing"))
                    continue
                audio = read_wav(clip.path(source))
                peaks.append(peak_db(audio))
                rmss.append(rms_db(audio))
                if peaks[-1] < QUIET_DB:
                    issues.append(Issue(clip, source, f"too quiet (peak {peaks[-1]:.0f} dBFS)"))
                if peaks[-1] > CLIP_DB:
                    issues.append(Issue(clip, source, "clipped"))
                cut = trimmed(clip, source, vad)
                if cut is None:
                    issues.append(Issue(clip, source, "no speech found"))
                else:
                    speech += len(read_wav(cut)) / SAMPLE_RATE
            summary[f"{name}/{source}"] = {
                "clips": len(peaks),
                "peak_db": float(np.median(peaks)) if peaks else None,
                "rms_db": float(np.median(rmss)) if rmss else None,
                "speech_s": round(speech, 1),
            }
    return summary, issues
