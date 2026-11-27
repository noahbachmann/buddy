"""Screenshot crops: the photo hash that tells photos apart (their tree nodes all read "<name>'s photo"), the JPEG a
decision call sees, and the pages of screenshots for a remote shadow session."""

import io

from PIL import Image

from .profile import dhash
from .uitree import Rect


def crop_hash(img: Image.Image, rect: Rect) -> str:
    """dhash of the crop. The section's own like button sits in the crop's corner: the same in every view."""
    small = img.crop((rect.left, rect.top, rect.right, rect.bottom)).convert("L").resize((9, 8), Image.LANCZOS)
    px = list(small.getdata())
    return dhash([px[r * 9:(r + 1) * 9] for r in range(8)])


def crop_jpeg(img: Image.Image, rect: Rect, max_side: int = 512, quality: int = 80) -> bytes:
    """The photo for the decision call: downscaled so its long side is `max_side`. Image tokens ≈ w × h / 750:
    a 4:5 photo is ~440 at 512, ~980 at 768."""
    crop = img.crop((rect.left, rect.top, rect.right, rect.bottom)).convert("RGB")
    crop.thumbnail((max_side, max_side), Image.LANCZOS)
    out = io.BytesIO()
    crop.save(out, "JPEG", quality=quality)
    return out.getvalue()


def shot_pages(shots: list[Image.Image], per_page: int = 4, status_bar: float = 0.04,
               gap: int = 10, quality: int = 80) -> list[bytes]:
    """Screenshots (taken at half size) side by side, `per_page` to a JPEG, without the status bar. Readable on a
    phone."""
    crops = [img.crop((0, int(img.height * status_bar), img.width, img.height)) for img in shots]
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
