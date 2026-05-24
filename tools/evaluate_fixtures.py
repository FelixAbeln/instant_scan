#!/usr/bin/env python3
"""Evaluate real instant-film fixtures and generate an HTML report.

This script is intentionally Python-only glue around the C library. The detector,
classifier, and extraction are still executed by libinstant_scan.
"""
from __future__ import annotations

import argparse
import html
import json
import shutil
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from instant_scan import InstantScan  # noqa: E402
from scan_image import choose_export_size  # noqa: E402


@dataclass
class EvalRow:
    file: str
    expected_film: str
    detected_film: str
    pass_: bool
    success: int
    confidence: float
    outer_aspect: float
    inner_aspect: float
    corrected_width: int
    corrected_height: int
    input_rel: str
    overlay_rel: str
    output_rel: str
    notes: str
    error: str
    corners: list[list[float]]


def load_expected(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    images = data.get("images", [])
    if not isinstance(images, list):
        raise ValueError(f"{path} must contain an 'images' list")
    return images


def safe_error(error_array: Any) -> str:
    return bytes(error_array).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def draw_overlay(image: Image.Image, corners: list[tuple[float, float]], out_path: Path) -> None:
    overlay = image.convert("RGBA")
    draw = ImageDraw.Draw(overlay, "RGBA")
    if len(corners) == 4:
        pts = [(float(x), float(y)) for x, y in corners]
        draw.line(pts + [pts[0]], fill=(0, 255, 0, 220), width=max(3, image.width // 220))
        radius = max(5, image.width // 160)
        labels = ["TL", "TR", "BR", "BL"]
        for label, (x, y) in zip(labels, pts):
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(255, 0, 0, 220))
            draw.text((x + radius + 2, y + radius + 2), label, fill=(255, 255, 255, 255))
    overlay.convert("RGB").save(out_path, quality=92)


def generate_html(rows: list[EvalRow], out_dir: Path, title: str = "instant_scan fixture evaluation") -> None:
    total = len(rows)
    passed = sum(1 for row in rows if row.pass_)
    avg_conf = sum(row.confidence for row in rows) / total if total else 0.0

    cards = []
    for row in rows:
        status = "pass" if row.pass_ else "fail"
        corners = "<br>".join(
            f"{name}: ({x:.1f}, {y:.1f})"
            for name, (x, y) in zip(["TL", "TR", "BR", "BL"], row.corners)
        )
        cards.append(
            f"""
            <section class="card {status}">
              <h2>{html.escape(row.file)}</h2>
              <div class="metrics">
                <div><b>Status</b><span>{'PASS' if row.pass_ else 'FAIL'}</span></div>
                <div><b>Expected</b><span>{html.escape(row.expected_film)}</span></div>
                <div><b>Detected</b><span>{html.escape(row.detected_film)}</span></div>
                <div><b>Confidence</b><span>{row.confidence:.3f}</span></div>
                <div><b>Outer aspect</b><span>{row.outer_aspect:.3f}</span></div>
                <div><b>Inner aspect</b><span>{row.inner_aspect:.3f}</span></div>
                <div><b>Corrected size</b><span>{row.corrected_width} × {row.corrected_height}</span></div>
                <div><b>Success</b><span>{row.success}</span></div>
              </div>
              <p class="notes">{html.escape(row.notes)}</p>
              {f'<p class="error">{html.escape(row.error)}</p>' if row.error else ''}
              <div class="images">
                <figure><img src="{html.escape(row.input_rel)}" loading="lazy"><figcaption>input</figcaption></figure>
                <figure><img src="{html.escape(row.overlay_rel)}" loading="lazy"><figcaption>detected corners</figcaption></figure>
                <figure><img src="{html.escape(row.output_rel)}" loading="lazy"><figcaption>extracted border crop</figcaption></figure>
              </div>
              <details><summary>corner coordinates</summary><code>{corners}</code></details>
            </section>
            """
        )

    html_text = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(title)}</title>
  <style>
    :root {{ color-scheme: light dark; font-family: system-ui, -apple-system, Segoe UI, sans-serif; }}
    body {{ margin: 0; padding: 24px; background: #f6f7f9; color: #202124; }}
    header {{ max-width: 1200px; margin: 0 auto 24px; }}
    h1 {{ margin: 0 0 8px; }}
    .summary {{ display: flex; gap: 12px; flex-wrap: wrap; }}
    .summary div {{ background: white; border-radius: 12px; padding: 12px 16px; box-shadow: 0 1px 4px #0001; }}
    main {{ max-width: 1200px; margin: 0 auto; display: grid; gap: 20px; }}
    .card {{ background: white; border-radius: 18px; padding: 18px; box-shadow: 0 2px 12px #0001; border-left: 8px solid #999; }}
    .card.pass {{ border-left-color: #16833a; }}
    .card.fail {{ border-left-color: #b42318; }}
    .metrics {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(145px, 1fr)); gap: 10px; margin: 12px 0; }}
    .metrics div {{ background: #f1f3f4; border-radius: 10px; padding: 10px; }}
    .metrics b {{ display: block; font-size: 12px; color: #5f6368; margin-bottom: 4px; }}
    .metrics span {{ font-size: 15px; }}
    .notes {{ color: #444; }}
    .error {{ color: #b42318; font-weight: 600; }}
    .images {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 14px; align-items: start; }}
    figure {{ margin: 0; }}
    img {{ width: 100%; height: auto; border-radius: 12px; background: #ddd; box-shadow: inset 0 0 0 1px #0001; }}
    figcaption {{ text-align: center; color: #5f6368; font-size: 13px; margin-top: 6px; }}
    code {{ display: block; padding: 10px; white-space: pre-wrap; background: #f1f3f4; border-radius: 10px; }}
    @media (prefers-color-scheme: dark) {{
      body {{ background: #111; color: #eee; }}
      .card, .summary div {{ background: #1d1d1d; }}
      .metrics div, code {{ background: #292929; }}
      .metrics b, figcaption {{ color: #aaa; }}
      .notes {{ color: #ccc; }}
    }}
  </style>
</head>
<body>
  <header>
    <h1>{html.escape(title)}</h1>
    <div class="summary">
      <div><b>Total</b><br>{total}</div>
      <div><b>Passed</b><br>{passed}</div>
      <div><b>Failed</b><br>{total - passed}</div>
      <div><b>Average confidence</b><br>{avg_conf:.3f}</div>
    </div>
  </header>
  <main>
    {''.join(cards)}
  </main>
</body>
</html>
"""
    (out_dir / "report.html").write_text(html_text, encoding="utf-8")


def evaluate(args: argparse.Namespace) -> int:
    fixtures_dir = Path(args.fixtures_dir)
    expected_path = Path(args.expected)
    out_dir = Path(args.out_dir)
    assets_dir = out_dir / "assets"
    inputs_dir = assets_dir / "inputs"
    overlays_dir = assets_dir / "overlays"
    exports_dir = assets_dir / "exports"
    for directory in [inputs_dir, overlays_dir, exports_dir]:
        directory.mkdir(parents=True, exist_ok=True)

    scanner = InstantScan(args.lib)
    rows: list[EvalRow] = []

    for item in load_expected(expected_path):
        filename = item["file"]
        expected_film = item["expected_film"]
        notes = item.get("notes", "")
        source_path = fixtures_dir / filename
        if not source_path.exists():
            raise FileNotFoundError(source_path)

        image = Image.open(source_path).convert("RGBA")
        rgba = np.asarray(image, dtype=np.uint8)
        result = scanner.scan_rgba(rgba)
        detected = scanner.film_name(result.film_type)
        corners = [(float(p.x), float(p.y)) for p in result.corners]
        error = safe_error(result.error)

        input_out = inputs_dir / source_path.name
        overlay_out = overlays_dir / f"{source_path.stem}_overlay.jpg"
        export_out = exports_dir / f"{source_path.stem}_instant_border.png"
        shutil.copy2(source_path, input_out)
        draw_overlay(image, corners if result.success else [], overlay_out)

        if result.success:
            out_w, out_h = choose_export_size(result, args.export_width)
            crop = scanner.extract_rgba(rgba, result, out_w, out_h)
            Image.fromarray(crop, mode="RGBA").save(export_out)
        else:
            Image.new("RGBA", (400, 280), (240, 240, 240, 255)).save(export_out)

        passed = bool(result.success) and detected == expected_film and result.confidence >= args.min_confidence
        rows.append(
            EvalRow(
                file=filename,
                expected_film=expected_film,
                detected_film=detected,
                pass_=passed,
                success=int(result.success),
                confidence=float(result.confidence),
                outer_aspect=float(result.outer_aspect),
                inner_aspect=float(result.inner_aspect),
                corrected_width=int(result.corrected_width),
                corrected_height=int(result.corrected_height),
                input_rel=str(input_out.relative_to(out_dir)),
                overlay_rel=str(overlay_out.relative_to(out_dir)),
                output_rel=str(export_out.relative_to(out_dir)),
                notes=notes,
                error=error,
                corners=[[float(x), float(y)] for x, y in corners],
            )
        )

    (out_dir / "metrics.json").write_text(
        json.dumps({"rows": [{**asdict(row), "pass": row.pass_} for row in rows]}, indent=2),
        encoding="utf-8",
    )
    generate_html(rows, out_dir)

    failed = [row for row in rows if not row.pass_]
    print(f"Wrote {out_dir / 'report.html'}")
    print(f"Passed {len(rows) - len(failed)} / {len(rows)}")
    for row in rows:
        status = "PASS" if row.pass_ else "FAIL"
        print(f"{status:4} {row.file}: expected={row.expected_film}, detected={row.detected_film}, confidence={row.confidence:.3f}")

    return 1 if failed and args.fail_on_mismatch else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate real fixture images and generate an HTML report.")
    parser.add_argument("--fixtures-dir", default=str(ROOT / "tests" / "fixtures" / "real"))
    parser.add_argument("--expected", default=str(ROOT / "tests" / "fixtures" / "real" / "expected.json"))
    parser.add_argument("--out-dir", default=str(ROOT / "build" / "evaluation"))
    parser.add_argument("--lib", default=str(ROOT / "build" / "libinstant_scan.so"))
    parser.add_argument("--export-width", type=int, default=1000)
    parser.add_argument("--min-confidence", type=float, default=0.20)
    parser.add_argument("--fail-on-mismatch", action="store_true", help="Exit non-zero when a fixture fails expectations")
    args = parser.parse_args()
    return evaluate(args)


if __name__ == "__main__":
    raise SystemExit(main())
