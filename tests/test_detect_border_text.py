from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from instant_scan import InstantScan  # noqa: E402


def draw_wide_with_border_marks() -> np.ndarray:
    canvas_w, canvas_h = 760, 560
    image = np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8)
    image[:, :, 0:3] = 28
    image[:, :, 3] = 255

    # Instax Wide, scale 5 px/mm: 108 x 86 mm.
    frame_w, frame_h = 540, 430
    x0 = (canvas_w - frame_w) // 2
    y0 = (canvas_h - frame_h) // 2
    x1 = x0 + frame_w
    y1 = y0 + frame_h

    image[y0:y1, x0:x1, 0:3] = 242

    # Image window: left/right 22 px, top 25 px, bottom 95 px.
    ix0 = x0 + 22
    iy0 = y0 + 25
    ix1 = x1 - 22
    iy1 = y1 - 95
    image[iy0:iy1, ix0:ix1, 0:3] = 75

    # Simulate handwriting/date marks on the white border. These deliberately
    # punch dark holes through the threshold mask but should remain captured in
    # the final crop.
    for i, xx in enumerate(range(x0 + 70, x0 + 390, 42)):
        y = y1 - 64 + (i % 3) * 9
        image[y:y + 8, xx:xx + 28, 0:3] = 35
        image[y + 14:y + 20, xx + 5:xx + 36, 0:3] = 45
    image[y0 + 9:y0 + 18, x0 + 130:x0 + 260, 0:3] = 55
    image[y0 + 13:y0 + 26, x0 + 310:x0 + 345, 0:3] = 45
    image[y0 + 160:y0 + 260, x0 + 8:x0 + 18, 0:3] = 40

    return image


def main() -> None:
    scanner = InstantScan(ROOT / "build" / "libinstant_scan.so")
    result = scanner.scan_rgba(draw_wide_with_border_marks())

    print("success:", result.success)
    print("film:", result.film_type, scanner.film_name(result.film_type))
    print("confidence:", round(result.confidence, 3))
    print("outer_aspect:", round(result.outer_aspect, 3))
    print("corners:", [(round(p.x, 1), round(p.y, 1)) for p in result.corners])

    assert result.success == 1
    assert scanner.film_name(result.film_type) == "Instax Wide"
    assert result.confidence > 0.7


if __name__ == "__main__":
    main()
