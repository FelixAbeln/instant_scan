from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from instant_scan import InstantScan  # noqa: E402


def draw_instant_frame(
    canvas_w: int,
    canvas_h: int,
    frame_w: int,
    frame_h: int,
    border: int = 26,
) -> np.ndarray:
    image = np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8)
    image[:, :, 0:3] = 35
    image[:, :, 3] = 255

    x0 = (canvas_w - frame_w) // 2
    y0 = (canvas_h - frame_h) // 2
    x1 = x0 + frame_w
    y1 = y0 + frame_h

    # White outer print.
    image[y0:y1, x0:x1, 0:3] = 245

    # Dark inner photo area, leaving a larger bottom border.
    inner_x0 = x0 + border
    inner_y0 = y0 + border
    inner_x1 = x1 - border
    inner_y1 = y1 - border * 2
    image[inner_y0:inner_y1, inner_x0:inner_x1, 0:3] = 70

    return image


def main() -> None:
    scanner = InstantScan(ROOT / "build" / "libinstant_scan.so")

    # Instax Mini ratio: 54 x 85. Scale by 4 px/mm.
    image = draw_instant_frame(
        canvas_w=520,
        canvas_h=620,
        frame_w=216,
        frame_h=340,
    )

    result = scanner.scan_rgba(image)
    print("success:", result.success)
    print("film:", result.film_type, scanner.film_name(result.film_type))
    print("confidence:", round(result.confidence, 3))
    print("outer_aspect:", round(result.outer_aspect, 3))
    print("corrected:", result.corrected_width, "x", result.corrected_height)
    print("corners:", [(round(p.x, 1), round(p.y, 1)) for p in result.corners])

    assert result.success == 1
    assert scanner.film_name(result.film_type) == "Instax Mini"
    assert result.confidence > 0.7


if __name__ == "__main__":
    main()
