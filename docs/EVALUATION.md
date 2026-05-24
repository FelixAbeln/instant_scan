# Real-image evaluation

The repository includes a small real-image fixture set under:

```text
tests/fixtures/real/
```

Each image has an expected film type listed in:

```text
tests/fixtures/real/expected.json
```

The evaluation script runs the C detector/classifier through the Python `ctypes` wrapper and writes a browsable HTML report.
The Python part is only test/report glue; the actual detection, classification, and extraction are executed by `libinstant_scan`.

## Run the report

Build the library first:

```bash
cmake -S . -B build
cmake --build build
```

Install helper dependencies:

```bash
python3 -m pip install -r requirements-dev.txt
```

Generate the report:

```bash
python3 tools/evaluate_fixtures.py --fail-on-mismatch
```

Open:

```text
build/evaluation/report.html
```

The report shows, for each fixture:

- the original input photo
- a corner overlay
- the extracted perspective-corrected crop including the border
- expected vs detected film type
- confidence
- outer aspect
- inner aspect
- corrected output dimensions
- corner coordinates

Machine-readable metrics are written to:

```text
build/evaluation/metrics.json
```

## Adding more test images

1. Copy the image into `tests/fixtures/real/`.
2. Add an entry to `tests/fixtures/real/expected.json`:

```json
{
  "file": "my_new_fixture.jpg",
  "expected_film": "Instax Wide",
  "notes": "Description of why this case matters."
}
```

3. Rerun:

```bash
python3 tools/evaluate_fixtures.py --fail-on-mismatch
```

Use the generated HTML to inspect false positives, bad corners, and export quality.
