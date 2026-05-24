from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

from instant_scan import InstantScan


def main():
    scanner = InstantScan()

    samples = [
        (54, 85),
        (72, 86),
        (108, 86),
        (88, 107),
        (53.9, 66.6),
        (100, 100),
    ]

    for width, height in samples:
        film_type, confidence, name = scanner.classify_by_outer_ratio(width, height)
        print(f"{width:6.1f} x {height:6.1f} -> {film_type:2d} {confidence:.3f} {name}")


if __name__ == "__main__":
    main()
