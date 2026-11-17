import json
from pathlib import Path

import numpy as np
import pytest

from buddy_voice import audio

CASES = json.loads((Path(__file__).parents[3] / "shared/tests/cases/audio_ctx.json").read_text())["cases"]


@pytest.mark.parametrize("samples, ctx", CASES)
def test_auto_audio_ctx_matches_ears(samples, ctx):
    assert audio.auto_audio_ctx(samples) == ctx


def test_clipping_counts_full_scale_and_longest_run():
    a = np.zeros(100, dtype=np.float32)
    assert audio.clipping(a) == (0, 0)
    a[[3, 10, 11, 12, 50, 51]] = [1.0, -1.0, 1.0, 1.0, 0.9995, -1.0]
    assert audio.clipping(a) == (6, 3)
