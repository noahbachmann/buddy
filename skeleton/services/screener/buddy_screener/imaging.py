"""Screenshot crops: the photo hash that tells photos apart (their tree nodes all read "<name>'s photo"), the JPEG a
decision call sees, and the pages of screenshots for a remote shadow session."""

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


def crop_jpeg(png: bytes, rect: Rect, max_side: int = 512, quality: int = 80) -> bytes:
    """The photo for the decision call: downscaled so its long side is `max_side`. Image tokens ≈ w × h / 750:
    a 4:5 photo is ~440 at 512, ~980 at 768."""
    with Image.open(io.BytesIO(png)) as img:
        crop = img.crop((rect.left, rect.top, rect.right, rect.bottom)).convert("RGB")
    crop.thumbnail((max_side, max_side), Image.LANCZOS)
    out = io.BytesIO()
    crop.save(out, "JPEG", quality=quality)
    return out.getvalue()


def shot_pages(pngs: list[bytes], per_page: int = 4, status_bar: float = 0.04, scale: float = 0.5,
               gap: int = 10, quality: int = 80) -> list[bytes]:
    """Screenshots side by side, `per_page` to a JPEG: without the status bar, at `scale`. Readable on a phone."""
    crops = []
    for png in pngs:
        with Image.open(io.BytesIO(png)) as img:
            w, h = img.size
            crop = img.convert("RGB").crop((0, int(h * status_bar), w, h))
        crops.append(crop.resize((int(crop.width * scale), int(crop.height * scale)), Image.LANCZOS))
    pages = []
    for i in range(0, len(crops), per_page):
        part = crops[i:i + per_page]
        page = Image.new("RGB", (sum(c.width for c in part) + gap * (len(part) - 1), max(c.height for c in part)),
                         "white")
        x = 0
        for c in part:
            page.paste(c, (x, 0))
            x += c.width + gap
        out = io.BytesIO()
        page.save(out, "JPEG", quality=quality)
        pages.append(out.getvalue())
    return pages
