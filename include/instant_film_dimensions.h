#ifndef INSTANT_FILM_DIMENSIONS_H
#define INSTANT_FILM_DIMENSIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Film dimension table.
 *
 * All dimensions are in millimeters. Keep this table as the single source of
 * truth for film sizes and border layout. The detector/classifier can be
 * improved without touching app code by updating these values or adding new
 * entries here.
 *
 * image_left/top/right/bottom_mm describe the visible image window position
 * inside the outer film frame, in the same orientation as outer_width_mm x
 * outer_height_mm.
 */

typedef enum {
    INSTANT_FILM_UNKNOWN = 0,
    INSTANT_FILM_INSTAX_MINI,
    INSTANT_FILM_INSTAX_SQUARE,
    INSTANT_FILM_INSTAX_WIDE,
    INSTANT_FILM_POLAROID_CLASSIC,
    INSTANT_FILM_POLAROID_GO
} instant_film_type;

typedef struct {
    instant_film_type type;
    const char *name;

    float outer_width_mm;
    float outer_height_mm;

    float image_width_mm;
    float image_height_mm;

    float image_left_mm;
    float image_top_mm;
    float image_right_mm;
    float image_bottom_mm;
} instant_film_dimensions;

static const instant_film_dimensions INSTANT_FILM_DIMENSIONS[] = {
    {
        INSTANT_FILM_INSTAX_MINI,
        "Instax Mini",
        54.0f, 85.0f,
        46.0f, 62.0f,
        4.0f, 4.0f, 4.0f, 19.0f
    },
    {
        INSTANT_FILM_INSTAX_SQUARE,
        "Instax Square",
        72.0f, 86.0f,
        62.0f, 62.0f,
        5.0f, 5.0f, 5.0f, 19.0f
    },
    {
        INSTANT_FILM_INSTAX_WIDE,
        "Instax Wide",
        108.0f, 86.0f,
        99.0f, 62.0f,
        4.5f, 5.0f, 4.5f, 19.0f
    },
    {
        INSTANT_FILM_POLAROID_CLASSIC,
        "Polaroid Classic / i-Type / 600 / SX-70",
        88.0f, 107.0f,
        79.0f, 77.0f,
        4.5f, 7.0f, 4.5f, 23.0f
    },
    {
        INSTANT_FILM_POLAROID_GO,
        "Polaroid Go",
        53.9f, 66.6f,
        47.0f, 46.0f,
        3.45f, 5.0f, 3.45f, 15.6f
    }
};

#define INSTANT_FILM_DIMENSIONS_COUNT \
    ((int)(sizeof(INSTANT_FILM_DIMENSIONS) / sizeof(INSTANT_FILM_DIMENSIONS[0])))

#ifdef __cplusplus
}
#endif

#endif
