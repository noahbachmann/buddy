"""Hugging Face Whisper → whisper.cpp GGML (what ears loads), plus an optional q8_0 version.

Uses whisper.cpp's own converter from the same pinned release as the C++ build (third_party/CMakeLists.txt),
and the mel filter bank from openai/whisper.
"""

import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

from huggingface_hub import hf_hub_download

from .fetch import download

# Same tag and checksum as skeleton/third_party/CMakeLists.txt: keep them equal
WHISPER_CPP = "1.9.5"
WHISPER_CPP_SHA256 = "ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720"
# openai/whisper's assets/mel_filters.npz at a pinned commit (the converter embeds it)
MEL_COMMIT = "86098128c0b4f24f0e2aa2994de830614b474227"
MEL_SHA256 = "7450ae70723a5ef9d341e3cee628c7cb0177f36ce42c44b7ed2bf3325f0f6d4c"


def whisper_cpp(cache: Path) -> Path:
    root = cache / f"whisper.cpp-{WHISPER_CPP}"
    if not root.exists():
        tgz = download(f"https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v{WHISPER_CPP}.tar.gz",
                       cache / f"whisper.cpp-{WHISPER_CPP}.tar.gz", WHISPER_CPP_SHA256)
        with tarfile.open(tgz) as t:
            t.extractall(cache, filter="data")
        tgz.unlink()
    return root


def openai_whisper_assets(cache: Path) -> Path:
    """A folder laid out like the openai/whisper repo, as far as the converter reads it."""
    root = cache / "openai-whisper"
    npz = root / "whisper" / "assets" / "mel_filters.npz"
    if not npz.exists():
        download(f"https://raw.githubusercontent.com/openai/whisper/{MEL_COMMIT}/whisper/assets/mel_filters.npz",
                 npz, MEL_SHA256)
    return root


def to_ggml(hf_dir: Path, base_repo: str, out: Path, cache: Path) -> Path:
    """hf_dir: save_pretrained() output. base_repo: e.g. openai/whisper-base.en (tokenizer files come from there:
    fine-tuning doesn't change them, and transformers 5 no longer writes the vocab.json the converter reads)."""
    for name in ("vocab.json", "added_tokens.json"):
        shutil.copy(hf_hub_download(base_repo, name), hf_dir / name)
    script = whisper_cpp(cache) / "models" / "convert-h5-to-ggml.py"
    tmp = out.parent / "convert"
    tmp.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(script), str(hf_dir), str(openai_whisper_assets(cache)), str(tmp)],
                   check=True, stdout=subprocess.DEVNULL)
    (tmp / "ggml-model.bin").rename(out)
    tmp.rmdir()
    return out


def quantize(model: Path, cache: Path, kind: str = "q8_0") -> Path | None:
    """Builds whisper.cpp's quantize tool once (needs cmake and a C++ compiler). None if they're missing."""
    if not shutil.which("cmake"):
        print("  (no cmake: skipping the quantized model)")
        return None
    root = whisper_cpp(cache)
    tool = root / "build" / "bin" / "whisper-quantize"
    if not tool.exists():
        print("  building whisper.cpp's quantize tool (once)", flush=True)
        subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build"), "-DCMAKE_BUILD_TYPE=Release",
                        "-DWHISPER_BUILD_TESTS=OFF", "-DWHISPER_BUILD_SERVER=OFF"],
                       check=True, capture_output=True)  # chatty, and git errors from the unpacked tarball
        subprocess.run(["cmake", "--build", str(root / "build"), "--target", "whisper-quantize", "-j"],
                       check=True, capture_output=True)
    out = model.with_name(model.stem + f"-{kind}.bin")
    subprocess.run([str(tool), str(model), str(out), kind], check=True, capture_output=True)
    return out
