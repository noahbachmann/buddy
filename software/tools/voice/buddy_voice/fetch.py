"""Pinned downloads with checksums (same pattern as deploy/pi/setup.sh)."""

import hashlib
import urllib.request
from pathlib import Path


def download(url: str, path: Path, sha256: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    part = path.with_suffix(path.suffix + ".part")
    print(f"  ↓ {url}", flush=True)
    urllib.request.urlretrieve(url, part)
    digest = hashlib.sha256(part.read_bytes()).hexdigest()
    if digest != sha256:
        part.unlink()
        raise RuntimeError(f"{url}: checksum {digest} != {sha256}")
    part.rename(path)
    return path
