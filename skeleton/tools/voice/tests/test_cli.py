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
