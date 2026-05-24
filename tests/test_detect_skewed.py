from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from instant_scan import InstantScan  # noqa: E402


def point_in_poly(x: float, y: float, poly: list[tuple[float, float]]) -> bool:
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        intersects = ((yi > y) != (yj > y)) and (
            x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-9) + xi
        )
        if intersects:
            inside = not inside
        j = i
    return inside


def fill_poly(image: np.ndarray, poly: list[tuple[float, float]], rgb: tuple[int, int, int]) -> None:
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    x0 = max(0, int(np.floor(min(xs))))
    x1 = min(image.shape[1] - 1, int(np.ceil(max(xs))))
    y0 = max(0, int(np.floor(min(ys))))
    y1 = min(image.shape[0] - 1, int(np.ceil(max(ys))))

    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if point_in_poly(x + 0.5, y + 0.5, poly):
                image[y, x, 0:3] = rgb


def quad_point(quad: list[tuple[float, float]], u: float, v: float) -> tuple[float, float]:
    tl, tr, br, bl = quad
    x = (1 - u) * (1 - v) * tl[0] + u * (1 - v) * tr[0] + u * v * br[0] + (1 - u) * v * bl[0]
    y = (1 - u) * (1 - v) * tl[1] + u * (1 - v) * tr[1] + u * v * br[1] + (1 - u) * v * bl[1]
    return x, y


def draw_skewed_instant_mini() -> np.ndarray:
    image = np.zeros((620, 640, 4), dtype=np.uint8)
    image[:, :, 0:3] = 35
    image[:, :, 3] = 255

    outer = [
        (200.0, 120.0),
        (416.0, 145.0),
        (376.0, 485.0),
        (160.0, 460.0),
    ]
    fill_poly(image, outer, (245, 245, 242))

    frame_w = 216.0
    frame_h = 340.0
    left = 26.0 / frame_w
    right = 1.0 - 26.0 / frame_w
    top = 26.0 / frame_h
    bottom = 1.0 - 52.0 / frame_h
    inner = [
        quad_point(outer, left, top),
        quad_point(outer, right, top),
        quad_point(outer, right, bottom),
        quad_point(outer, left, bottom),
    ]
    fill_poly(image, inner, (68, 68, 68))
    return image


def main() -> None:
    scanner = InstantScan(ROOT / "build" / "libinstant_scan.so")
    image = draw_skewed_instant_mini()
    result = scanner.scan_rgba(image)

    print("success:", result.success)
    print("film:", result.film_type, scanner.film_name(result.film_type))
    print("confidence:", round(result.confidence, 3))
    print("outer_aspect:", round(result.outer_aspect, 3))
    print("corrected:", result.corrected_width, "x", result.corrected_height)
    print("corners:", [(round(p.x, 1), round(p.y, 1)) for p in result.corners])

    assert result.success == 1
    assert scanner.film_name(result.film_type) == "Instax Mini"
    assert result.confidence > 0.65
    assert abs(result.outer_aspect - (85.0 / 54.0)) < 0.02


if __name__ == "__main__":
    main()
