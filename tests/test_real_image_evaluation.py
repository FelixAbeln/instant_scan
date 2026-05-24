#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    report_dir = ROOT / "build" / "evaluation"
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "evaluate_fixtures.py"),
        "--out-dir",
        str(report_dir),
        "--fail-on-mismatch",
    ]
    subprocess.run(cmd, check=True)
    assert (report_dir / "report.html").exists(), "report.html was not created"
    assert (report_dir / "metrics.json").exists(), "metrics.json was not created"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
