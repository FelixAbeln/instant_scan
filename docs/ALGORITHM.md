# Detection algorithm

This document describes the current detector at a high level. The implementation lives mostly in:

```text
src/detect.c
```

## Goals

The detector should find an instant-film print in a casual phone photo and return:

- the four outer corners,
- the likely film type,
- confidence,
- dimensions for a corrected crop,
- an extractable crop that includes the full white border.

## Current pipeline

```text
RGBA input
→ white-ish mask
→ morphological closing
→ connected components
→ choose likely frame component
→ extract exterior boundary points
→ fit four outer lines
→ intersect lines to estimate corners
→ compare against film templates
→ return result
```

## White mask

Pixels are considered part of the possible frame when they are bright and low-saturation enough to be likely white border material.

The mask is intentionally generous so slightly dirty, warm, blue, or shadowed borders can still be detected.

## Text tolerance

Users often write dates or notes on the border. Dark writing can punch holes in a pure white-threshold mask.

To handle this, the detector:

1. Repairs the mask with morphological closing.
2. Treats interior holes as noise.
3. Fits lines from exterior boundary regions rather than all white pixels.

This allows the output crop to preserve the handwriting while preventing the handwriting from dominating the frame geometry.

## Film classification

Classification uses the dimension table in:

```text
include/instant_film_dimensions.h
```

The table contains outer dimensions, inner image dimensions, and margins. The detector can score candidates using both outer-frame geometry and expected image-window layout. This is more robust than ratio-only classification when perspective distortion makes the projected rectangle look too square.

## Extraction

`instant_extract_rgba` uses the detected four outer corners and performs a perspective warp into a caller-provided RGBA buffer.

The crop includes the entire outer print, not just the inner photo area. This is important because the border may contain handwritten notes.

## Known limitations

Current difficult cases:

- white or very bright background merging with the border,
- severe glare on the border,
- border text covering most of an outer edge,
- multiple instant prints in one image,
- very blurred or tiny prints,
- strong shadows that make one edge no longer look white.

## Useful future improvements

1. Add debug image exports for mask, component, corner overlay, and final warp.
2. Add multi-candidate detection for images containing multiple prints.
3. Add an explicit inner-photo extraction API.
4. Improve white-background handling with edge gradients and shadow cues.
5. Add optional orientation normalization so the larger bottom border is always placed at the bottom of exports.
6. Add a small Android JNI example project.
