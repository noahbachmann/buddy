"""WAV I/O, levels, and the audio_ctx formula shared with ears (services/ears/stt.hpp)."""

import math
from pathlib import Path

import numpy as np
import soundfile as sf

SAMPLE_RATE = 16000
AUDIO_CTX_MARGIN = 16  # = kAudioCtxMargin in services/ears/stt.hpp


def auto_audio_ctx(samples: int) -> int:
    """Encoder frames whisper.cpp uses with BUDDY_STT_AUDIO_CTX=auto: 50 per second plus a margin, max 1500.

    ears pads commands shorter than 1.1 s with silence first; so does pad_short().
    """
    return min(math.ceil(samples / 320) + AUDIO_CTX_MARGIN, 1500)


def pad_short(audio: np.ndarray) -> np.ndarray:
    """whisper.cpp skips input shorter than 1 s, so ears pads to 1.1 s (services/ears/stt.cpp). Same here."""
    need = SAMPLE_RATE * 11 // 10
    return audio if len(audio) >= need else np.pad(audio, (0, need - len(audio)))


def read_wav(path: Path) -> np.ndarray:
    """16 kHz mono → float32 in [-1, 1]. Raises on anything else (record.sh always writes that format)."""
    audio, sr = sf.read(path, dtype="float32", always_2d=False)
    if sr != SAMPLE_RATE or audio.ndim != 1:
        raise ValueError(f"{path}: need 16 kHz mono, got {sr} Hz, {audio.ndim} channel(s)")
    return audio


def write_wav(path: Path, audio: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, audio, SAMPLE_RATE, subtype="PCM_16")


def dbfs(x: float) -> float:
    return 20 * math.log10(max(x, 1e-5))


def peak_db(audio: np.ndarray) -> float:
    return dbfs(float(np.max(np.abs(audio)))) if len(audio) else -100.0


def rms_db(audio: np.ndarray) -> float:
    return dbfs(float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))) if len(audio) else -100.0
