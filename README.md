# instant-scan

`instant-scan` is a small C library for detecting, classifying, and extracting instant-film prints from phone photos.

It is designed for use in an Android app later, so the main image-processing code lives in plain C with a stable C ABI. The Python code in this repository is only a development/test wrapper around the C library.

Current capabilities:

- Detects the white outer frame of Instax/Polaroid-style prints.
- Handles moderate skew and perspective distortion.
- Preserves handwritten text on the border in exported crops.
- Classifies common instant-film formats from dimensions and border layout.
- Exports a perspective-corrected image including the full white border.
- Provides Python `ctypes` hooks for quick testing.

## Supported film formats

The editable film table is in:

```text
include/instant_film_dimensions.h
```

Currently included:

| Film type | Outer size | Image area |
|---|---:|---:|
| Instax Mini | 54 × 85 mm | 46 × 62 mm |
| Instax Square | 72 × 86 mm | 62 × 62 mm |
| Instax Wide | 108 × 86 mm | 99 × 62 mm |
| Polaroid Classic / i-Type / 600 / SX-70 | 88 × 107 mm | 79 × 77 mm |
| Polaroid Go | 53.9 × 66.6 mm | 47 × 46 mm |

Polaroid i-Type, 600, and SX-70 share the same outer format and are intentionally grouped as `Polaroid Classic`.

## Repository layout

```text
include/
  instant_scan.h              Public C API
  instant_film_dimensions.h   Film dimensions and margins
src/
  detect.c                    Detection, line fitting, extraction
  classify.c                  Film classification
  geometry.c                  Reserved geometry helpers
  image_ops.c                 Reserved image helpers
python/
  instant_scan.py             Python ctypes wrapper
tools/
  scan_image.py               CLI for testing real photos
tests/
  test_ratio.py               Ratio classifier smoke test
  test_detect_synthetic.py    Synthetic clean-frame test
  test_detect_skewed.py       Synthetic skew test
  test_detect_border_text.py  Handwriting/noisy-border test
android/
  CMakeLists.txt              Android NDK integration starter
docs/
  API.md
  ANDROID.md
  ALGORITHM.md
```

## Build

Requirements:

- CMake 3.16+
- C99-compatible compiler
- Python 3.10+ for the helper scripts/tests
- `numpy` and `Pillow` for Python tools

Build the C library:

```bash
cmake -S . -B build
cmake --build build
```

This creates a shared library such as:

```text
build/libinstant_scan.so
```

On macOS or Windows the file extension/name will differ depending on the platform.

## Run tests

Install Python helper dependencies:

```bash
python3 -m pip install -r requirements-dev.txt
```

Build first, then run:

```bash
python3 tests/test_ratio.py
python3 tests/test_detect_synthetic.py
python3 tests/test_detect_skewed.py
python3 tests/test_detect_border_text.py
```

## Scan a real image

```bash
python3 tools/scan_image.py path/to/photo.jpg --export-dir exports --export-width 1600
```

Example output:

```text
photo.jpg
  image: 709 x 1536
  success: 1
  film_type: 3 (Instax Wide)
  confidence: 0.998
  outer_aspect: 1.256
  inner_aspect: 1.597
  corrected_size: 108 x 86
  corners:
    TL: (23.0, 263.0)
    TR: (691.0, 263.0)
    BR: (691.0, 1107.0)
    BL: (23.0, 1107.0)
  exported: exports/photo_instant_border.png (1600 x 1274)
```

The exported image includes the full border, so handwritten notes/dates on the border remain visible.

## C usage

```c
#include "instant_scan.h"

instant_options options = instant_default_options();

instant_result result = instant_scan_rgba(
    rgba_pixels,
    width,
    height,
    stride_bytes,
    options
);

if (result.success) {
    printf("film: %s\n", instant_film_type_name(result.film_type));
}
```

To export the corrected crop including the full border:

```c
int ok = instant_extract_rgba(
    rgba_pixels,
    width,
    height,
    stride_bytes,
    result.corners,
    output_width,
    output_height,
    output_rgba,
    output_stride_bytes
);
```

See [docs/API.md](docs/API.md) for full API details.

## Python usage

```python
from pathlib import Path
import numpy as np
from PIL import Image
from instant_scan import InstantScan

scanner = InstantScan(Path("build/libinstant_scan.so"))
image = Image.open("photo.jpg").convert("RGBA")
rgba = np.asarray(image, dtype=np.uint8)

result = scanner.scan_rgba(rgba)
print(scanner.film_name(result.film_type), result.confidence)

if result.success:
    crop = scanner.extract_rgba(rgba, result, 1600, 1274)
    Image.fromarray(crop, mode="RGBA").save("photo_instant_border.png")
```

## Android direction

The Android app should not use the Python wrapper. Use the C library through JNI/NDK:

```text
Android Bitmap / camera frame
→ RGBA buffer
→ JNI wrapper
→ instant_scan_rgba(...)
→ instant_extract_rgba(...)
→ Kotlin/Java result object + optional Bitmap crop
```

See [docs/ANDROID.md](docs/ANDROID.md) for the intended Android integration path.

## Detection notes

The detector currently works best when the instant print is visible against a non-white background. It is designed to tolerate:

- moderate perspective distortion,
- dark handwriting on the border,
- normal border dirt/specks,
- slightly imperfect lighting.

Hard cases that may still need future work:

- white table/background merging with the border,
- severe glare on the border,
- writing covering most of an outer edge,
- multiple instant prints in the same photo,
- very low-resolution or heavily blurred images.

See [docs/ALGORITHM.md](docs/ALGORITHM.md) for implementation details and future improvements.

## License

MIT. See [LICENSE](LICENSE).
