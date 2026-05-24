#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from instant_scan import InstantScan  # noqa: E402


def choose_export_size(result, export_width: int) -> tuple[int, int]:
    w = max(1, int(result.corrected_width))
    h = max(1, int(result.corrected_height))
    if w >= h:
        out_w = export_width
        out_h = max(1, round(export_width * h / w))
    else:
        out_h = export_width
        out_w = max(1, round(export_width * w / h))
    return out_w, out_h


def choose_inner_export_size(result, export_width: int) -> tuple[int, int]:
    outer_w, outer_h = choose_export_size(result, export_width)
    scale_x = outer_w / max(1, int(result.corrected_width))
    scale_y = outer_h / max(1, int(result.corrected_height))
    inner_w = max(1, round(int(result.inner_corrected_width) * scale_x))
    inner_h = max(1, round(int(result.inner_corrected_height) * scale_y))
    return inner_w, inner_h


def _white_fraction_edge(rgba: np.ndarray, edge: str) -> float:
    rgb = rgba[:, :, :3].astype(np.int16)
    maxc = rgb.max(axis=2)
    minc = rgb.min(axis=2)
    luma = (77 * rgb[:, :, 0] + 150 * rgb[:, :, 1] + 29 * rgb[:, :, 2]) >> 8
    mask = (luma > 170) & ((maxc - minc) < 85)

    h, _ = mask.shape
    band = max(1, int(round(h * 0.10)))
    if edge == "top":
        return float(mask[:band, :].mean())
    if edge == "bottom":
        return float(mask[h - band :, :].mean())
    raise ValueError(edge)


def _film_prefers_landscape(film_type: int) -> bool | None:
    # Keep the output in the usual viewing orientation for each film family.
    # Instax Wide is landscape; Mini, Polaroid Classic and Go are portrait.
    # Square has no strong aspect preference, so leave it alone.
    if film_type == 3:  # INSTANT_FILM_INSTAX_WIDE
        return True
    if film_type in (1, 4, 5):  # Mini, Polaroid Classic, Polaroid Go
        return False
    return None


def canonical_rotation_k_for_crop(rgba: np.ndarray, film_type: int) -> int:
    """Return np.rot90(k) needed to put the export into canonical viewing orientation.

    When two rotations satisfy the desired aspect, the version with the
    stronger white bottom border is chosen. This uses the photo geometry:
    instant film's larger chemical/bottom border should end up at the bottom.
    """
    prefer_landscape = _film_prefers_landscape(film_type)
    if prefer_landscape is None:
        return 0

    candidates = []
    for k in (0, 1, 2, 3):
        candidate = np.rot90(rgba, k)
        h, w = candidate.shape[:2]
        aspect_ok = (w >= h) if prefer_landscape else (h >= w)
        if not aspect_ok:
            continue
        bottom = _white_fraction_edge(candidate, "bottom")
        top = _white_fraction_edge(candidate, "top")
        candidates.append((bottom - top, k))

    if not candidates:
        return 0

    # Largest bottom-vs-top white margin wins. Prefer no rotation on ties.
    candidates.sort(key=lambda item: (item[0], -abs(item[1])), reverse=True)
    return int(candidates[0][1])


def apply_rotation_k(rgba: np.ndarray, k: int) -> np.ndarray:
    return np.ascontiguousarray(np.rot90(rgba, k)) if k else rgba



def main() -> int:
    parser = argparse.ArgumentParser(description="Scan real Instax/Polaroid photos with libinstant_scan.")
    parser.add_argument("images", nargs="+", help="Image files to scan")
    parser.add_argument("--lib", default=str(ROOT / "build" / "libinstant_scan.so"), help="Path to libinstant_scan shared library")
    parser.add_argument("--export-dir", default=None, help="Directory to save perspective-corrected crops")
    parser.add_argument("--export-width", type=int, default=1600, help="Longest side of exported full-print crop in pixels")
    parser.add_argument("--export-inner", action="store_true", help="Also save the inner visible image without the border")
    parser.add_argument("--export-both", action="store_true", help="Save both the full print and the inner visible image")
    args = parser.parse_args()

    scanner = InstantScan(args.lib)
    export_dir = Path(args.export_dir) if args.export_dir else None
    if export_dir:
        export_dir.mkdir(parents=True, exist_ok=True)

    want_inner = args.export_inner or args.export_both

    for image_path in args.images:
        path = Path(image_path)
        image = Image.open(path).convert("RGBA")
        rgba = np.asarray(image, dtype=np.uint8)
        result = scanner.scan_rgba(rgba)

        print(f"\n{path.name}")
        print(f"  image: {image.width} x {image.height}")
        print(f"  success: {result.success}")
        print(f"  film_type: {result.film_type} ({scanner.film_name(result.film_type)})")
        print(f"  confidence: {result.confidence:.3f}")
        print(f"  outer_aspect: {result.outer_aspect:.3f}")
        print(f"  inner_aspect: {result.inner_aspect:.3f}")
        print(f"  corrected_outer_size: {result.corrected_width} x {result.corrected_height}")
        print(f"  corrected_inner_size: {result.inner_corrected_width} x {result.inner_corrected_height}")
        print("  outer corners:")
        for label, point in zip(["TL", "TR", "BR", "BL"], result.corners):
            print(f"    {label}: ({point.x:.1f}, {point.y:.1f})")
        print("  inner corners:")
        for label, point in zip(["TL", "TR", "BR", "BL"], result.inner_corners):
            print(f"    {label}: ({point.x:.1f}, {point.y:.1f})")

        error = bytes(result.error).split(b"\0", 1)[0].decode("utf-8", errors="replace")
        if error:
            print(f"  note: {error}")

        if export_dir and result.success:
            outer_w, outer_h = choose_export_size(result, args.export_width)
            crop = scanner.extract_rgba(rgba, result, outer_w, outer_h)
            rotation_k = canonical_rotation_k_for_crop(crop, int(result.film_type))
            crop = apply_rotation_k(crop, rotation_k)
            out_path = export_dir / f"{path.stem}_instant_border.png"
            Image.fromarray(crop, mode="RGBA").save(out_path)
            print(f"  exported border: {out_path} ({crop.shape[1]} x {crop.shape[0]})")

            if want_inner:
                inner_w, inner_h = choose_inner_export_size(result, args.export_width)
                inner = scanner.extract_inner_rgba(rgba, result, inner_w, inner_h)
                inner = apply_rotation_k(inner, rotation_k)
                inner_path = export_dir / f"{path.stem}_instant_inner.png"
                Image.fromarray(inner, mode="RGBA").save(inner_path)
                print(f"  exported inner:  {inner_path} ({inner.shape[1]} x {inner.shape[0]})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
