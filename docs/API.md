# C API

The public API is declared in:

```text
include/instant_scan.h
```

The library accepts raw RGBA pixels and does not depend on any image-file loader. This is intentional: Android, Python, desktop apps, and tests can all provide RGBA buffers in their own way.

## Data types

### `instant_point`

```c
typedef struct {
    float x;
    float y;
} instant_point;
```

A 2D point in source-image pixel coordinates.

### `instant_options`

```c
typedef struct {
    int max_output_width;
    int return_warped_image;
    float min_confidence;
} instant_options;
```

Current fields:

| Field | Meaning |
|---|---|
| `max_output_width` | Reserved for future internal extraction/scaling. |
| `return_warped_image` | Reserved for a future API that allocates/returns a crop. Current extraction uses `instant_extract_rgba`. |
| `min_confidence` | Minimum confidence hint. Current detector still returns its best result and sets `success` according to internal checks. |

Use `instant_default_options()` unless you need to experiment.

### `instant_result`

```c
typedef struct {
    int success;
    instant_film_type film_type;
    float confidence;

    instant_point corners[4];

    int corrected_width;
    int corrected_height;

    float outer_aspect;
    float inner_aspect;

    char error[128];
} instant_result;
```

| Field | Meaning |
|---|---|
| `success` | Non-zero if a likely instant print was found. |
| `film_type` | Detected film format. See `instant_film_type`. |
| `confidence` | Approximate confidence from `0.0` to `1.0`. |
| `corners` | Detected outer corners in source image order: top-left, top-right, bottom-right, bottom-left. |
| `corrected_width` | Suggested corrected crop width. Currently based on the detected film template dimensions. |
| `corrected_height` | Suggested corrected crop height. |
| `outer_aspect` | Outer frame aspect ratio used by classifier. |
| `inner_aspect` | Inner image-window aspect ratio from film template. |
| `error` | Human-readable note/error string. Empty on normal success. |

## Film types

Declared in `include/instant_film_dimensions.h`:

```c
typedef enum {
    INSTANT_FILM_UNKNOWN = 0,
    INSTANT_FILM_INSTAX_MINI,
    INSTANT_FILM_INSTAX_SQUARE,
    INSTANT_FILM_INSTAX_WIDE,
    INSTANT_FILM_POLAROID_CLASSIC,
    INSTANT_FILM_POLAROID_GO
} instant_film_type;
```

Use `instant_film_type_name(type)` to get a display name.

## `instant_default_options`

```c
instant_options instant_default_options(void);
```

Returns sensible defaults.

## `instant_scan_rgba`

```c
instant_result instant_scan_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    instant_options options
);
```

Scans a source image for an instant-film print.

Arguments:

| Argument | Meaning |
|---|---|
| `rgba` | Pointer to the first byte of the RGBA image. Pixel order is R, G, B, A. |
| `width` | Image width in pixels. |
| `height` | Image height in pixels. |
| `stride` | Bytes per row. Usually `width * 4`, but may be larger on Android/bitmap buffers. |
| `options` | Options returned by `instant_default_options()`. |

Returns an `instant_result`. If `success != 0`, the `corners` field can be passed to `instant_extract_rgba`.

## `instant_extract_rgba`

```c
int instant_extract_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point corners[4],
    int output_width,
    int output_height,
    unsigned char *out_rgba,
    int out_stride
);
```

Perspective-warps the detected outer frame into a flat RGBA output image, including the full white border.

The function does not allocate memory. The caller must allocate `out_rgba`.

Arguments:

| Argument | Meaning |
|---|---|
| `rgba` | Source RGBA pixels. |
| `width`, `height`, `stride` | Source image geometry. |
| `corners` | Four outer corners from `instant_result.corners`. |
| `output_width` | Desired output width in pixels. |
| `output_height` | Desired output height in pixels. |
| `out_rgba` | Caller-owned destination buffer. |
| `out_stride` | Bytes per output row. Usually `output_width * 4`. |

Returns non-zero on success.

## `instant_classify_film_by_outer_ratio`

```c
instant_film_type instant_classify_film_by_outer_ratio(
    float outer_width,
    float outer_height,
    float *out_confidence
);
```

Classifies a film format using only outer width/height ratio. This is useful for tests and fallback logic, but it is less reliable than `instant_scan_rgba` for perspective-distorted photos.

## Updating dimensions

Update:

```text
include/instant_film_dimensions.h
```

Each row has:

```c
{
    type,
    "Display name",
    outer_width_mm, outer_height_mm,
    image_width_mm, image_height_mm,
    image_left_mm, image_top_mm, image_right_mm, image_bottom_mm
}
```

Keep dimensions in millimeters. Margins describe the visible image window position inside the outer frame.
