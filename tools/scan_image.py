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


def main() -> int:
    parser = argparse.ArgumentParser(description="Scan real Instax/Polaroid photos with libinstant_scan.")
    parser.add_argument("images", nargs="+", help="Image files to scan")
    parser.add_argument("--lib", default=str(ROOT / "build" / "libinstant_scan.so"), help="Path to libinstant_scan shared library")
    parser.add_argument("--export-dir", default=None, help="Directory to save perspective-corrected crops including the full border")
    parser.add_argument("--export-width", type=int, default=1600, help="Longest side of exported crop in pixels")
    args = parser.parse_args()

    scanner = InstantScan(args.lib)
    export_dir = Path(args.export_dir) if args.export_dir else None
    if export_dir:
        export_dir.mkdir(parents=True, exist_ok=True)

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
        print(f"  corrected_size: {result.corrected_width} x {result.corrected_height}")
        print("  corners:")
        for label, point in zip(["TL", "TR", "BR", "BL"], result.corners):
            print(f"    {label}: ({point.x:.1f}, {point.y:.1f})")

        error = bytes(result.error).split(b"\0", 1)[0].decode("utf-8", errors="replace")
        if error:
            print(f"  note: {error}")

        if export_dir and result.success:
            out_w, out_h = choose_export_size(result, args.export_width)
            crop = scanner.extract_rgba(rgba, result, out_w, out_h)
            out_path = export_dir / f"{path.stem}_instant_border.png"
            Image.fromarray(crop, mode="RGBA").save(out_path)
            print(f"  exported: {out_path} ({out_w} x {out_h})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
