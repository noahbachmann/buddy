"""Screenshot crops: the photo hash that tells photos apart (their tree nodes all read "<name>'s photo")."""

import io

from PIL import Image

from .profile import dhash
from .uitree import Rect


def crop_hash(png: bytes, rect: Rect) -> str:
    """dhash of the crop. The section's own like button sits in the crop's corner: the same in every view."""
    with Image.open(io.BytesIO(png)) as img:
        small = img.crop((rect.left, rect.top, rect.right, rect.bottom)).convert("L").resize((9, 8), Image.LANCZOS)
        px = list(small.getdata())
    return dhash([px[r * 9:(r + 1) * 9] for r in range(8)])
