import pytest

from buddy_voice.score import norm, word_errors


@pytest.mark.parametrize("a, b", [
    ("Set the volume to 50%.", "set the volume to fifty percent"),
    ("Play my playlist Mhm, please.", "play my playlist mhm please"),
    ("What's playing?", "whats playing"),
    ("Volume twenty five", "volume 25"),
    ("Skip this one.", "skip this one"),
])
def test_norm_equal(a, b):
    assert norm(a) == norm(b)


def test_word_errors():
    assert word_errors("Play City Lights by James Gruntz.", "Play City Lights by James Crops") == (1, 6)
    assert word_errors("Pause.", "") == (1, 1)
    assert word_errors("Next song.", "Next song.") == (0, 2)


def test_live_score_splits_whisper_errors_from_brain_gaps():
    from buddy_voice.score import live

    intents = {"All lights off.": ("off", "", "all"), "on lights off.": ("unknown", "", ""),
               "Turn on lights off.": ("unknown", "", ""), "Pause.": ("pause", "", "")}
    s = live([("live/00001.wav", "All lights off.", "All lights off."),
              ("live/00002.wav", "on lights off.", "All lights off."),
              ("live/00003.wav", "Pause.", "Pause."),
              ("live/00004.wav", "Turn on lights off.", "Turn on lights off.")], intents)
    assert (s["clips"], s["understood"], s["intent"], s["exact"]) == (4, 3, 2, 3)
    assert s["gaps"] == ["Turn on lights off."]
