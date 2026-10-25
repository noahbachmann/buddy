"""Values that must match the C++ side: the audio_ctx formula and the whisper.cpp release."""

import re
from pathlib import Path

from buddy_voice import audio, convert

SKELETON = Path(__file__).resolve().parents[3]


def test_audio_ctx_margin_matches_ears():
    hpp = (SKELETON / "services/ears/stt.hpp").read_text()
    assert int(re.search(r"kAudioCtxMargin = (\d+);", hpp).group(1)) == audio.AUDIO_CTX_MARGIN
    assert "(samples + 319) / 320" in hpp


def test_auto_audio_ctx():
    assert audio.auto_audio_ctx(16000) == 50 + audio.AUDIO_CTX_MARGIN
    assert audio.auto_audio_ctx(16001) == 51 + audio.AUDIO_CTX_MARGIN
    assert audio.auto_audio_ctx(16000 * 60) == 1500


def test_whisper_cpp_release_matches_cmake():
    cmake = (SKELETON / "third_party/CMakeLists.txt").read_text()
    assert f"tags/v{convert.WHISPER_CPP}.tar.gz" in cmake
    assert convert.WHISPER_CPP_SHA256 in cmake
