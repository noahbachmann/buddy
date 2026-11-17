import csv

import numpy as np
import pytest

from buddy_voice import audio


@pytest.fixture
def voice_dir(tmp_path, monkeypatch):
    from buddy_voice import data

    monkeypatch.setattr(data, "DATA", tmp_path / "voice")
    monkeypatch.setattr(data, "SESSIONS", tmp_path / "voice" / "sessions")
    return data


def old_folder(tmp_path, rows, files):
    src = tmp_path / "old"
    (src / "clips").mkdir(parents=True)
    with open(src / "metadata.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file_name", "transcription"])
        w.writerows(rows)
    for name in files:
        audio.write_wav(src / "clips" / name, np.zeros(1600, dtype=np.float32))
    return src


def test_import_repairs_old_numbering(tmp_path, voice_dir):
    src = old_folder(tmp_path, [
        ("clips/00001.wav", "Play Spotify."),
        ("clips/00002.wav", "Are the kitchen lights on?"),  # overwritten by the next run's first clip
        ("clips/00002.wav", "Pause."),
        ("clips/00003.wav", "Next song."),                   # deleted by hand
    ], ["00001.wav", "00002.wav"])
    notes = voice_dir.import_old(src, "s1", "train")
    assert len(notes) == 2
    clips = voice_dir.load_session("s1")
    assert [(c.file, c.sentence) for c in clips] == [("00001.wav", "Play Spotify."), ("00002.wav", "Pause.")]
    assert clips[0].path("aec").exists()


def test_trim_keeps_lead_and_tail():
    from buddy_voice import vad

    class FakeVad(vad.Vad):
        def __init__(self):
            pass

        def speech_span(self, a):
            return (16000, 32000)

    cut = FakeVad().trim(np.zeros(16000 * 4, dtype=np.float32))
    assert len(cut) == 16000 + int((vad.LEAD_S + vad.TAIL_S) * 16000)


class _NoTrim:
    def trim(self, audio):
        return audio


def test_check_flags_only_real_clipping(voice_dir):
    def clip(*ranges):
        a = np.full(16000, 0.1, dtype=np.float32)
        for lo, hi in ranges:
            a[lo:hi] = 1.0
        return a

    cases = {
        "00001.wav": clip((100, 101)),                                # lone touch, like live/00073
        "00002.wav": clip(*[(i, i + 3) for i in range(0, 16000, 1300)]),  # 39 in runs of 3, like live/00092
        "00003.wav": clip((500, 510)),                                # flattened for 0.6 ms
        "00004.wav": clip(*[(i, i + 2) for i in range(0, 16000, 300)]),   # 1 % scattered
    }
    for name, a in cases.items():
        audio.write_wav(voice_dir.SESSIONS / "live" / "raw" / name, a)
    voice_dir.write_session("live", [voice_dir.Clip("live", n, "Pause.", "train") for n in cases])
    _, issues = voice_dir.check(_NoTrim(), ["live"])
    assert sorted(i.clip.file for i in issues if i.problem.startswith("clipped")) == ["00003.wav", "00004.wav"]
