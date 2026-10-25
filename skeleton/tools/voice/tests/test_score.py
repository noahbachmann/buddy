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
