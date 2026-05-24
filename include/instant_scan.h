#ifndef INSTANT_SCAN_H
#define INSTANT_SCAN_H

#include "instant_film_dimensions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x;
    float y;
} instant_point;

typedef struct {
    int max_output_width;
    int return_warped_image;
    float min_confidence;
} instant_options;

typedef struct {
    int success;
    instant_film_type film_type;
    float confidence;

    instant_point corners[4]; /* top-left, top-right, bottom-right, bottom-left */

    int corrected_width;
    int corrected_height;

    float outer_aspect;
    float inner_aspect;

    char error[128];
} instant_result;

instant_options instant_default_options(void);

instant_result instant_scan_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    instant_options options
);

/*
 * Perspective-warp an already detected outer film quadrilateral into a flat
 * RGBA image. The crop includes the full outer border. The caller owns
 * out_rgba and chooses the output size. Returns non-zero on success.
 */
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

instant_film_type instant_classify_film_by_outer_ratio(
    float outer_width,
    float outer_height,
    float *out_confidence
);

const char *instant_film_type_name(instant_film_type type);

#ifdef __cplusplus
}
#endif

#endif
