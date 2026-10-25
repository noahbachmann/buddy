"""Silero VAD, the same model and frame layout as ears (services/ears/vad.cpp), for trimming clips."""

from pathlib import Path

import numpy as np
import onnxruntime as ort

from .audio import SAMPLE_RATE

# Same file and checksum as deploy/pi/fetch-models.sh
URL = "https://raw.githubusercontent.com/snakers4/silero-vad/v6.2.3/src/silero_vad/data/silero_vad.onnx"
SHA256 = "1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3"
FRAME = 512    # 32 ms, fixed by the model
CONTEXT = 64   # samples of the previous frame the model expects in front
LEAD_S = 0.1   # kept before the first speech frame
TAIL_S = 0.3   # kept after the last one (live commands end ~0.7 s after speech; the tail of a recording has the
               # Enter-key click, which must not be in the training data)


def _model(cache: Path) -> Path:
    path = cache / "silero_vad.onnx"
    if not path.exists():
        from .fetch import download  # noqa: PLC0415

        download(URL, path, SHA256)
    return path


class Vad:
    def __init__(self, cache: Path, threshold: float = 0.5):
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 1
        self.session = ort.InferenceSession(str(_model(cache)), opts, providers=["CPUExecutionProvider"])
        self.threshold = threshold

    def probabilities(self, audio: np.ndarray) -> np.ndarray:
        """Speech probability per 32 ms frame."""
        state = np.zeros((2, 1, 128), dtype=np.float32)
        context = np.zeros(CONTEXT, dtype=np.float32)
        sr = np.array(SAMPLE_RATE, dtype=np.int64)
        probs = []
        for start in range(0, len(audio) - FRAME + 1, FRAME):
            frame = audio[start : start + FRAME].astype(np.float32)
            x = np.concatenate([context, frame])[None, :]
            out, state = self.session.run(None, {"input": x, "state": state, "sr": sr})
            context = frame[-CONTEXT:]
            probs.append(float(out[0][0]))
        return np.array(probs)

    def speech_span(self, audio: np.ndarray) -> tuple[int, int] | None:
        """First and last speech sample, or None if there is no speech (≥ 3 frames = 96 ms, like ears)."""
        speech = np.flatnonzero(self.probabilities(audio) >= self.threshold)
        if len(speech) < 3:
            return None
        return int(speech[0] * FRAME), int((speech[-1] + 1) * FRAME)

    def trim(self, audio: np.ndarray) -> np.ndarray | None:
        span = self.speech_span(audio)
        if span is None:
            return None
        start = max(0, span[0] - int(LEAD_S * SAMPLE_RATE))
        end = min(len(audio), span[1] + int(TAIL_S * SAMPLE_RATE))
        return audio[start:end]
