import json
from pathlib import Path

import pytest

from buddy_voice import audio

CASES = json.loads((Path(__file__).parents[3] / "shared/tests/cases/audio_ctx.json").read_text())["cases"]


@pytest.mark.parametrize("samples, ctx", CASES)
def test_auto_audio_ctx_matches_ears(samples, ctx):
    assert audio.auto_audio_ctx(samples) == ctx
