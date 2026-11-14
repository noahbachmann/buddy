import argparse

import numpy as np
import pytest

from buddy_voice import audio, cli


@pytest.fixture
def live(tmp_path, monkeypatch):
    from buddy_voice import data

    monkeypatch.setattr(data, "DATA", tmp_path / "voice")
    monkeypatch.setattr(data, "SESSIONS", tmp_path / "voice" / "sessions")
    monkeypatch.setattr(cli, "_player", lambda: None)
    pi_commands = []
    monkeypatch.setattr(cli.pi, "run", lambda command, **_: pi_commands.append(command) or "")
    monkeypatch.setattr(cli, "cmd_pull", lambda _args=None: "/home/dev/buddy/voice-live")
    monkeypatch.setattr(cli.pi, "brain", lambda texts: pi_commands.append(f"brain {texts}") or {})
    for stem, heard in (("20261028-100000", "Play Muse."), ("20261028-100100", "Plate news."),
                        ("20261028-100200", "Hmm."), ("20261028-100300", "Pause.")):
        audio.write_wav(data.DATA / "live" / f"{stem}.wav", np.zeros(1600, dtype=np.float32))
        (data.DATA / "live" / f"{stem}.txt").write_text(heard + "\n")
    return data, pi_commands


def answers(monkeypatch, *replies):
    it = iter(replies)
    monkeypatch.setattr("builtins.input", lambda _prompt: next(it))


def test_history_labels_and_deletes_handled_clips_on_pi(live, monkeypatch):
    data, pi_commands = live
    answers(monkeypatch, "", "Play Muse.", "d", "s")
    cli.cmd_history(argparse.Namespace(source="raw"))
    pi_commands.pop()  # the live score's brain run

    clips = data.load_session("live")
    assert [(c.file, c.sentence, c.set) for c in clips] == [("00001.wav", "Play Muse.", "train"),
                                                            ("00002.wav", "Play Muse.", "train")]
    assert (data.SESSIONS / "live" / "raw" / "00002.wav").exists()
    assert [p.stem for p in (data.DATA / "live").glob("*.wav")] == ["20261028-100300"]  # skipped: stays
    assert len(pi_commands) == 1
    cmd = pi_commands[0]
    assert cmd.startswith("cd /home/dev/buddy/voice-live && rm -f -- ")
    for stem in ("20261028-100000", "20261028-100100", "20261028-100200"):
        assert f"{stem}.wav" in cmd and f"{stem}.txt" in cmd
    assert "20261028-100300" not in cmd


def test_history_deletes_on_pi_after_ctrl_c(live, monkeypatch):
    data, pi_commands = live
    replies = iter([""])

    def reply(_prompt):
        try:
            return next(replies)
        except StopIteration:
            raise KeyboardInterrupt from None

    monkeypatch.setattr("builtins.input", reply)
    with pytest.raises(KeyboardInterrupt):
        cli.cmd_history(argparse.Namespace(source="raw"))
    assert len(data.load_session("live")) == 1
    assert "20261028-100000.wav" in pi_commands[0] and "20261028-100100" not in pi_commands[0]


def test_history_numbers_on_after_existing_clips(live, monkeypatch):
    data, _ = live
    data.write_session("live", [data.Clip("live", "00007.wav", "Next track.", "train")])
    answers(monkeypatch, "q")
    cli.cmd_history(argparse.Namespace(source="raw"))
    answers(monkeypatch, "", "q")
    cli.cmd_history(argparse.Namespace(source="raw"))
    assert [c.file for c in data.load_session("live")] == ["00007.wav", "00008.wav"]


def test_history_saves_the_live_score(live, monkeypatch):
    data, _ = live
    monkeypatch.setattr(cli.pi, "brain", lambda texts: {"Play Muse.": ("play", "muse", ""), "Plate news.": ("play", "plate news", "")})
    answers(monkeypatch, "", "Play Muse.", "q")
    cli.cmd_history(argparse.Namespace(source="raw"))
    import json

    [report] = (data.DATA / "evals").glob("live-*.json")
    s = json.loads(report.read_text())
    assert (s["clips"], s["understood"], s["intent"]) == (2, 2, 1)


def test_prompts_lists_only_sentences_not_recorded_before_today(live, monkeypatch, capsys):
    import time

    data, _ = live
    today = time.strftime("%Y-%m-%d")
    train = [c.sentence for c in data.corpus.load() if c.set == "train"]
    data.write_session("2026-10-28-desk", [data.Clip("2026-10-28-desk", "00001.wav", train[0], "train")])
    data.write_session("live", [data.Clip("live", "00001.wav", train[1].lower().rstrip(".?!"), "train")])
    data.write_session(f"{today}-desk", [data.Clip(f"{today}-desk", "00001.wav", train[4], "train")])
    pi_out = (f"/home/dev/buddy/voice/2026-11-01-far/metadata.csv:file,sentence,set\n"
              f'/home/dev/buddy/voice/2026-11-01-far/metadata.csv:00001.wav,"{train[2]}",train\n'
              f'/home/dev/buddy/voice/2026-11-01-far/metadata.csv:00002.wav,"{train[3]}",test\n')
    monkeypatch.setattr(cli.pi, "run", lambda command, **_: pi_out)
    cli.cmd_prompts(argparse.Namespace(set="train"))
    out = [line.split("\t") for line in capsys.readouterr().out.splitlines()]
    assert all(s == "train" for s, _ in out)
    printed = [t for _, t in out]
    assert not {train[0], train[1], train[2]} & set(printed)
    assert train[3] in printed  # its clip on the Pi is a test clip
    assert train[4] in printed  # recorded today: a second session today (box) still gets it
    assert len(printed) == len(train) - 3
