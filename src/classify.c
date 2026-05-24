#include "instant_scan.h"

#include <math.h>
#include <stddef.h>

static float normalized_ratio(float width, float height) {
    if (width <= 0.0f || height <= 0.0f) {
        return 0.0f;
    }
    return width > height ? width / height : height / width;
}

const char *instant_film_type_name(instant_film_type type) {
    for (int i = 0; i < INSTANT_FILM_DIMENSIONS_COUNT; ++i) {
        if (INSTANT_FILM_DIMENSIONS[i].type == type) {
            return INSTANT_FILM_DIMENSIONS[i].name;
        }
    }
    return "Unknown";
}

instant_film_type instant_classify_film_by_outer_ratio(
    float outer_width,
    float outer_height,
    float *out_confidence
) {
    const float ratio = normalized_ratio(outer_width, outer_height);
    if (ratio <= 0.0f) {
        if (out_confidence) *out_confidence = 0.0f;
        return INSTANT_FILM_UNKNOWN;
    }

    float best_error = 99999.0f;
    instant_film_type best_type = INSTANT_FILM_UNKNOWN;

    for (int i = 0; i < INSTANT_FILM_DIMENSIONS_COUNT; ++i) {
        const instant_film_dimensions *film = &INSTANT_FILM_DIMENSIONS[i];
        const float expected = normalized_ratio(film->outer_width_mm, film->outer_height_mm);
        const float error = fabsf(ratio - expected);

        if (error < best_error) {
            best_error = error;
            best_type = film->type;
        }
    }

    /*
     * This threshold is intentionally conservative.
     * Ratio-only classification can confuse square-ish formats.
     */
    const float max_error = 0.045f;

    if (best_error > max_error) {
        if (out_confidence) *out_confidence = 0.0f;
        return INSTANT_FILM_UNKNOWN;
    }

    if (out_confidence) {
        float confidence = 1.0f - (best_error / max_error);
        if (confidence < 0.0f) confidence = 0.0f;
        if (confidence > 1.0f) confidence = 1.0f;
        *out_confidence = confidence;
    }

    return best_type;
}
