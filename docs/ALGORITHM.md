# Algorithm notes

For a visual explanation, start here:

- [Pipeline overview](PIPELINE.md)
- [Math and theory](THEORY.md)

The implementation is intentionally dependency-free C. The main pipeline is:

```text
RGBA image
→ white-border mask
→ morphological closing
→ connected components
→ exterior boundary
→ line fitting
→ line intersections
→ film-template scoring
→ inner-window refinement
→ perspective extraction
→ export orientation normalization
```

The most important implementation file is:

```text
src/detect.c
```

The film dimensions and margins are in:

```text
include/instant_film_dimensions.h
```
