#include "instant_scan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define INSTANT_LINE_VERTICAL_EPS 1.0e-4f

typedef struct {
    float a;
    float b;
    float c;
} instant_line;

static instant_result make_error(const char *message) {
    instant_result result;
    memset(&result, 0, sizeof(result));
    result.success = 0;
    result.film_type = INSTANT_FILM_UNKNOWN;
    if (message) {
        strncpy(result.error, message, sizeof(result.error) - 1);
    }
    return result;
}

static float point_distance(instant_point a, instant_point b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

static float normalized_ratio(float width, float height) {
    if (width <= 0.0f || height <= 0.0f) {
        return 0.0f;
    }
    return width > height ? width / height : height / width;
}

static const instant_film_dimensions *find_film_dimensions(instant_film_type type) {
    for (int i = 0; i < INSTANT_FILM_DIMENSIONS_COUNT; ++i) {
        if (INSTANT_FILM_DIMENSIONS[i].type == type) {
            return &INSTANT_FILM_DIMENSIONS[i];
        }
    }
    return NULL;
}

static int corners_are_physically_landscape(const instant_point corners[4]) {
    const float top = point_distance(corners[0], corners[1]);
    const float bottom = point_distance(corners[3], corners[2]);
    const float left = point_distance(corners[0], corners[3]);
    const float right = point_distance(corners[1], corners[2]);
    const float horiz = 0.5f * (top + bottom);
    const float vert = 0.5f * (left + right);
    return horiz >= vert;
}

static void rotate_template_90cw(
    float *outer_w,
    float *outer_h,
    float *image_w,
    float *image_h,
    float *left_margin,
    float *top_margin,
    float *right_margin,
    float *bottom_margin
) {
    const float old_outer_w = *outer_w;
    const float old_outer_h = *outer_h;
    const float old_image_w = *image_w;
    const float old_image_h = *image_h;
    const float old_left = *left_margin;
    const float old_top = *top_margin;
    const float old_right = *right_margin;
    const float old_bottom = *bottom_margin;

    *outer_w = old_outer_h;
    *outer_h = old_outer_w;
    *image_w = old_image_h;
    *image_h = old_image_w;
    *left_margin = old_bottom;
    *top_margin = old_left;
    *right_margin = old_top;
    *bottom_margin = old_right;
}

static int is_white_border_pixel(const unsigned char *p) {
    const int r = p[0];
    const int g = p[1];
    const int b = p[2];
    const int maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const int luma = (77 * r + 150 * g + 29 * b) >> 8;

    /*
     * Bright and roughly neutral. This allows warm/yellowed instant-film
     * borders while rejecting most colorful scene content.
     */
    return luma >= 188 && maxc >= 205 && (maxc - minc) <= 70;
}


static void dilate_mask_3x3(const unsigned char *src, unsigned char *dst, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char v = 0;
            for (int yy = y - 1; yy <= y + 1 && !v; ++yy) {
                if (yy < 0 || yy >= height) continue;
                for (int xx = x - 1; xx <= x + 1; ++xx) {
                    if (xx < 0 || xx >= width) continue;
                    if (src[yy * width + xx]) { v = 1; break; }
                }
            }
            dst[y * width + x] = v;
        }
    }
}

static void erode_mask_3x3(const unsigned char *src, unsigned char *dst, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char v = 1;
            for (int yy = y - 1; yy <= y + 1 && v; ++yy) {
                if (yy < 0 || yy >= height) { v = 0; break; }
                for (int xx = x - 1; xx <= x + 1; ++xx) {
                    if (xx < 0 || xx >= width || !src[yy * width + xx]) { v = 0; break; }
                }
            }
            dst[y * width + x] = v;
        }
    }
}

static int close_mask(unsigned char *mask, int width, int height, int iterations) {
    const size_t n = (size_t)width * (size_t)height;
    unsigned char *tmp = (unsigned char *)malloc(n);
    if (!tmp) return 0;
    for (int i = 0; i < iterations; ++i) {
        dilate_mask_3x3(mask, tmp, width, height);
        memcpy(mask, tmp, n);
    }
    for (int i = 0; i < iterations; ++i) {
        erode_mask_3x3(mask, tmp, width, height);
        memcpy(mask, tmp, n);
    }
    free(tmp);
    return 1;
}

static void flood_exterior_noncomponent(
    const unsigned char *component,
    int width,
    int height,
    unsigned char *outside,
    int *queue
) {
    const int pixel_count = width * height;
    int head = 0;
    int tail = 0;

    for (int x = 0; x < width; ++x) {
        const int top = x;
        const int bottom = (height - 1) * width + x;
        if (!component[top] && !outside[top]) { outside[top] = 1; queue[tail++] = top; }
        if (!component[bottom] && !outside[bottom]) { outside[bottom] = 1; queue[tail++] = bottom; }
    }
    for (int y = 0; y < height; ++y) {
        const int left = y * width;
        const int right = y * width + width - 1;
        if (!component[left] && !outside[left]) { outside[left] = 1; queue[tail++] = left; }
        if (!component[right] && !outside[right]) { outside[right] = 1; queue[tail++] = right; }
    }

    while (head < tail) {
        const int idx = queue[head++];
        const int x = idx % width;
        const int y = idx / width;
        const int neigh[4] = {
            x > 0 ? idx - 1 : -1,
            x + 1 < width ? idx + 1 : -1,
            y > 0 ? idx - width : -1,
            y + 1 < height ? idx + width : -1
        };
        for (int k = 0; k < 4; ++k) {
            const int nidx = neigh[k];
            if (nidx >= 0 && nidx < pixel_count && !component[nidx] && !outside[nidx]) {
                outside[nidx] = 1;
                queue[tail++] = nidx;
            }
        }
    }
}

static int is_exterior_component_boundary_pixel(
    const unsigned char *component,
    const unsigned char *outside,
    int width,
    int height,
    int x,
    int y
) {
    const int idx = y * width + x;
    if (!component[idx]) return 0;
    if (x == 0 || y == 0 || x == width - 1 || y == height - 1) return 1;
    return outside[idx - 1] || outside[idx + 1] ||
           outside[idx - width] || outside[idx + width];
}

static int line_from_points(instant_point p0, instant_point p1, instant_line *line) {
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;
    const float norm = sqrtf(dx * dx + dy * dy);
    if (norm < 1.0e-4f) {
        return 0;
    }

    line->a = dy / norm;
    line->b = -dx / norm;
    line->c = -(line->a * p0.x + line->b * p0.y);
    return 1;
}

static float abs_line_distance(instant_line line, float x, float y) {
    return fabsf(line.a * x + line.b * y + line.c);
}

static int intersect_lines(instant_line l1, instant_line l2, instant_point *out) {
    const float det = l1.a * l2.b - l2.a * l1.b;
    if (fabsf(det) < 1.0e-5f) {
        return 0;
    }

    out->x = (l1.b * l2.c - l2.b * l1.c) / det;
    out->y = (l1.c * l2.a - l2.c * l1.a) / det;
    return 1;
}

static void update_extreme_points(
    int x,
    int y,
    instant_point *tl,
    instant_point *tr,
    instant_point *br,
    instant_point *bl,
    float *min_sum,
    float *max_sum,
    float *max_diff,
    float *min_diff
) {
    const float xf = (float)x;
    const float yf = (float)y;
    const float sum = xf + yf;
    const float diff = xf - yf;

    if (sum < *min_sum) {
        *min_sum = sum;
        tl->x = xf;
        tl->y = yf;
    }
    if (sum > *max_sum) {
        *max_sum = sum;
        br->x = xf;
        br->y = yf;
    }
    if (diff > *max_diff) {
        *max_diff = diff;
        tr->x = xf;
        tr->y = yf;
    }
    if (diff < *min_diff) {
        *min_diff = diff;
        bl->x = xf;
        bl->y = yf;
    }
}

static int fit_line_least_squares(const instant_point *points, int count, instant_line *line) {
    if (count < 2) {
        return 0;
    }

    float mean_x = 0.0f;
    float mean_y = 0.0f;
    for (int i = 0; i < count; ++i) {
        mean_x += points[i].x;
        mean_y += points[i].y;
    }
    mean_x /= (float)count;
    mean_y /= (float)count;

    float sxx = 0.0f;
    float syy = 0.0f;
    float sxy = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float dx = points[i].x - mean_x;
        const float dy = points[i].y - mean_y;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }

    if (sxx + syy < 1.0e-4f) {
        return 0;
    }

    /*
     * Principal-axis fit. The line direction is the dominant eigenvector of
     * the 2x2 covariance matrix; the line normal is perpendicular to it.
     */
    const float theta = 0.5f * atan2f(2.0f * sxy, sxx - syy);
    const float vx = cosf(theta);
    const float vy = sinf(theta);
    line->a = -vy;
    line->b = vx;
    line->c = -(line->a * mean_x + line->b * mean_y);

    const float norm = sqrtf(line->a * line->a + line->b * line->b);
    if (norm < 1.0e-4f) {
        return 0;
    }
    line->a /= norm;
    line->b /= norm;
    line->c /= norm;
    return 1;
}

static int fit_side_line(
    const instant_point *boundary,
    int boundary_count,
    instant_point corner_a,
    instant_point corner_b,
    instant_line *out_line
) {
    instant_line seed;
    if (!line_from_points(corner_a, corner_b, &seed)) {
        return 0;
    }

    const float side_len = point_distance(corner_a, corner_b);
    float tolerance = side_len * 0.045f;
    if (tolerance < 5.0f) tolerance = 5.0f;
    if (tolerance > 24.0f) tolerance = 24.0f;

    instant_point *side_points = (instant_point *)malloc((size_t)boundary_count * sizeof(instant_point));
    if (!side_points) {
        return 0;
    }

    int side_count = 0;
    const float ax = corner_a.x;
    const float ay = corner_a.y;
    const float bx = corner_b.x;
    const float by = corner_b.y;
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len2 = dx * dx + dy * dy;

    for (int i = 0; i < boundary_count; ++i) {
        const float px = boundary[i].x;
        const float py = boundary[i].y;
        const float dist = abs_line_distance(seed, px, py);
        if (dist > tolerance) {
            continue;
        }

        float t = ((px - ax) * dx + (py - ay) * dy) / len2;
        if (t < -0.08f || t > 1.08f) {
            continue;
        }

        side_points[side_count++] = boundary[i];
    }

    int ok = fit_line_least_squares(side_points, side_count, out_line);
    free(side_points);
    return ok;
}

static int is_component_boundary_pixel(const unsigned char *component, int width, int height, int x, int y) {
    const int idx = y * width + x;
    if (!component[idx]) {
        return 0;
    }

    if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
        return 1;
    }

    return !component[idx - 1] || !component[idx + 1] ||
           !component[idx - width] || !component[idx + width];
}

static int estimate_corners_from_component(
    const int *component_pixels,
    int component_count,
    int width,
    int height,
    instant_point out_corners[4]
) {
    unsigned char *component = (unsigned char *)calloc((size_t)width * (size_t)height, sizeof(unsigned char));
    unsigned char *outside = (unsigned char *)calloc((size_t)width * (size_t)height, sizeof(unsigned char));
    int *flood_queue = (int *)malloc((size_t)width * (size_t)height * sizeof(int));
    instant_point *boundary = (instant_point *)malloc((size_t)component_count * sizeof(instant_point));
    if (!component || !outside || !flood_queue || !boundary) {
        free(component);
        free(outside);
        free(flood_queue);
        free(boundary);
        return 0;
    }

    for (int i = 0; i < component_count; ++i) {
        component[component_pixels[i]] = 1;
    }

    /*
     * Mark non-component pixels connected to the image exterior. When fitting
     * the film's outer sides, this deliberately ignores interior holes: the
     * image window, handwriting, stamps, and dirt on the white border should
     * not become side-line evidence.
     */
    flood_exterior_noncomponent(component, width, height, outside, flood_queue);

    instant_point tl = {0.0f, 0.0f};
    instant_point tr = {0.0f, 0.0f};
    instant_point br = {0.0f, 0.0f};
    instant_point bl = {0.0f, 0.0f};
    float min_sum = 1.0e30f;
    float max_sum = -1.0e30f;
    float max_diff = -1.0e30f;
    float min_diff = 1.0e30f;

    int boundary_count = 0;
    for (int i = 0; i < component_count; ++i) {
        const int idx = component_pixels[i];
        const int x = idx % width;
        const int y = idx / width;
        if (is_exterior_component_boundary_pixel(component, outside, width, height, x, y)) {
            boundary[boundary_count].x = (float)x;
            boundary[boundary_count].y = (float)y;
            update_extreme_points(x, y, &tl, &tr, &br, &bl, &min_sum, &max_sum, &max_diff, &min_diff);
            ++boundary_count;
        }
    }

    if (boundary_count < 16) {
        free(component);
        free(outside);
        free(flood_queue);
        free(boundary);
        return 0;
    }

    instant_line top, right, bottom, left;
    int ok = fit_side_line(boundary, boundary_count, tl, tr, &top) &&
             fit_side_line(boundary, boundary_count, tr, br, &right) &&
             fit_side_line(boundary, boundary_count, bl, br, &bottom) &&
             fit_side_line(boundary, boundary_count, tl, bl, &left);

    if (ok) {
        ok = intersect_lines(top, left, &out_corners[0]) &&
             intersect_lines(top, right, &out_corners[1]) &&
             intersect_lines(bottom, right, &out_corners[2]) &&
             intersect_lines(bottom, left, &out_corners[3]);
    }

    if (!ok) {
        out_corners[0] = tl;
        out_corners[1] = tr;
        out_corners[2] = br;
        out_corners[3] = bl;
        ok = 1;
    }

    free(component);
    free(outside);
    free(flood_queue);
    free(boundary);
    return ok;
}


static float clampf_local(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

typedef struct {
    float h[9];
} instant_homography;

static int solve_8x8(float a[8][9], float x[8]) {
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        float best = fabsf(a[col][col]);
        for (int row = col + 1; row < 8; ++row) {
            float v = fabsf(a[row][col]);
            if (v > best) { best = v; pivot = row; }
        }
        if (best < 1.0e-8f) return 0;
        if (pivot != col) {
            for (int k = col; k < 9; ++k) {
                float tmp = a[col][k]; a[col][k] = a[pivot][k]; a[pivot][k] = tmp;
            }
        }
        float div = a[col][col];
        for (int k = col; k < 9; ++k) a[col][k] /= div;
        for (int row = 0; row < 8; ++row) {
            if (row == col) continue;
            float f = a[row][col];
            if (fabsf(f) < 1.0e-12f) continue;
            for (int k = col; k < 9; ++k) a[row][k] -= f * a[col][k];
        }
    }
    for (int i = 0; i < 8; ++i) x[i] = a[i][8];
    return 1;
}

static int homography_from_canonical_to_source(
    float canonical_w,
    float canonical_h,
    const instant_point corners[4],
    instant_homography *out
) {
    const float sx[4] = {0.0f, canonical_w, canonical_w, 0.0f};
    const float sy[4] = {0.0f, 0.0f, canonical_h, canonical_h};
    float m[8][9];
    memset(m, 0, sizeof(m));

    for (int i = 0; i < 4; ++i) {
        const float x = sx[i];
        const float y = sy[i];
        const float u = corners[i].x;
        const float v = corners[i].y;
        const int r0 = i * 2;
        const int r1 = r0 + 1;

        m[r0][0] = x; m[r0][1] = y; m[r0][2] = 1.0f;
        m[r0][6] = -u * x; m[r0][7] = -u * y; m[r0][8] = u;

        m[r1][3] = x; m[r1][4] = y; m[r1][5] = 1.0f;
        m[r1][6] = -v * x; m[r1][7] = -v * y; m[r1][8] = v;
    }

    float h[8];
    if (!solve_8x8(m, h)) return 0;
    out->h[0] = h[0]; out->h[1] = h[1]; out->h[2] = h[2];
    out->h[3] = h[3]; out->h[4] = h[4]; out->h[5] = h[5];
    out->h[6] = h[6]; out->h[7] = h[7]; out->h[8] = 1.0f;
    return 1;
}

static instant_point homography_map_point(const instant_homography *h, float x, float y) {
    const float den = h->h[6] * x + h->h[7] * y + h->h[8];
    instant_point p;
    if (fabsf(den) < 1.0e-8f) {
        p.x = -1.0f;
        p.y = -1.0f;
        return p;
    }
    p.x = (h->h[0] * x + h->h[1] * y + h->h[2]) / den;
    p.y = (h->h[3] * x + h->h[4] * y + h->h[5]) / den;
    return p;
}

static float sample_white_score(const unsigned char *rgba, int width, int height, int stride, float fx, float fy) {
    int x = (int)(fx + 0.5f);
    int y = (int)(fy + 0.5f);
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 0.0f;
    }
    const unsigned char *p = rgba + (size_t)y * (size_t)stride + (size_t)x * 4u;
    const float r = (float)p[0];
    const float g = (float)p[1];
    const float b = (float)p[2];
    const float maxc = fmaxf(r, fmaxf(g, b));
    const float minc = fminf(r, fminf(g, b));
    const float luma = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
    const float chroma = (maxc - minc) / 255.0f;
    const float neutral = 1.0f - clampf_local(chroma / 0.34f, 0.0f, 1.0f);
    const float bright_gate = clampf_local((luma - 0.45f) / 0.40f, 0.0f, 1.0f);
    return clampf_local((0.72f * luma + 0.28f * neutral) * bright_gate, 0.0f, 1.0f);
}

static float edge_transition_score(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_homography *homography,
    float x0,
    float y0,
    float x1,
    float y1,
    float outside_dx,
    float outside_dy,
    float inside_dx,
    float inside_dy
) {
    const int samples = 40;
    float sum = 0.0f;
    int count = 0;
    for (int i = 2; i < samples - 2; ++i) {
        const float t = (float)i / (float)(samples - 1);
        const float x = x0 + (x1 - x0) * t;
        const float y = y0 + (y1 - y0) * t;
        instant_point po = homography_map_point(homography, x + outside_dx, y + outside_dy);
        instant_point pi = homography_map_point(homography, x + inside_dx, y + inside_dy);
        const float wo = sample_white_score(rgba, width, height, stride, po.x, po.y);
        const float wi = sample_white_score(rgba, width, height, stride, pi.x, pi.y);
        /* We primarily need a white border outside the image. Contrast helps,
           but some real pictures contain bright monitors/windows, so do not
           over-penalize a bright inner sample. */
        float s = 0.70f * wo + 0.30f * clampf_local(wo - wi + 0.25f, 0.0f, 1.0f);
        sum += s;
        ++count;
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

static float border_fill_score(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_homography *homography,
    float x0,
    float y0,
    float x1,
    float y1
) {
    const int nx = 8;
    const int ny = 8;
    float sum = 0.0f;
    int count = 0;
    for (int yy = 0; yy < ny; ++yy) {
        const float y = y0 + (y1 - y0) * ((float)yy + 0.5f) / (float)ny;
        for (int xx = 0; xx < nx; ++xx) {
            const float x = x0 + (x1 - x0) * ((float)xx + 0.5f) / (float)nx;
            instant_point p = homography_map_point(homography, x, y);
            sum += sample_white_score(rgba, width, height, stride, p.x, p.y);
            ++count;
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

typedef struct {
    instant_film_type type;
    float confidence;
    float score;
    float canonical_w;
    float canonical_h;
    float image_w;
    float image_h;
    float inner_aspect;
    float left_margin;
    float top_margin;
    float right_margin;
    float bottom_margin;
    int rotated;
} instant_template_match;

static float score_template_orientation(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point corners[4],
    const instant_film_dimensions *film,
    int rotated,
    instant_template_match *out
) {
    const float ow = rotated ? film->outer_height_mm : film->outer_width_mm;
    const float oh = rotated ? film->outer_width_mm : film->outer_height_mm;
    const float iw = rotated ? film->image_height_mm : film->image_width_mm;
    const float ih = rotated ? film->image_width_mm : film->image_height_mm;

    if (ow <= 0.0f || oh <= 0.0f || iw <= 0.0f || ih <= 0.0f || iw >= ow || ih >= oh) {
        return 0.0f;
    }

    instant_homography h;
    if (!homography_from_canonical_to_source(ow, oh, corners, &h)) {
        return 0.0f;
    }

    float left_margin = film->image_left_mm;
    float top_margin = film->image_top_mm;
    float right_margin = film->image_right_mm;
    float bottom_margin = film->image_bottom_mm;

    if (rotated) {
        /* Rotate the physical template 90 degrees clockwise. */
        const float old_left = left_margin;
        const float old_top = top_margin;
        const float old_right = right_margin;
        const float old_bottom = bottom_margin;
        left_margin = old_bottom;
        top_margin = old_left;
        right_margin = old_top;
        bottom_margin = old_right;
    }

    if (left_margin <= 0.5f || right_margin <= 0.5f || top_margin <= 0.5f || bottom_margin <= 0.5f) {
        return 0.0f;
    }

    const float ix0 = left_margin;
    const float ix1 = ow - right_margin;
    const float iy0 = top_margin;
    const float iy1 = oh - bottom_margin;

    if (fabsf((ix1 - ix0) - iw) > 2.0f || fabsf((iy1 - iy0) - ih) > 2.0f) {
        return 0.0f;
    }

    const float off_side = clampf_local(fminf(left_margin, right_margin) * 0.50f, 1.0f, 5.0f);
    const float off_top = clampf_local(top_margin * 0.50f, 1.0f, 5.0f);
    const float off_bottom = clampf_local(bottom_margin * 0.35f, 1.0f, 7.0f);
    const float off_inside = clampf_local(fminf(iw, ih) * 0.035f, 1.0f, 5.0f);

    const float top_edge = edge_transition_score(rgba, width, height, stride, &h, ix0, iy0, ix1, iy0, 0.0f, -off_top, 0.0f, off_inside);
    const float bottom_edge = edge_transition_score(rgba, width, height, stride, &h, ix0, iy1, ix1, iy1, 0.0f, off_bottom, 0.0f, -off_inside);
    const float left_edge = edge_transition_score(rgba, width, height, stride, &h, ix0, iy0, ix0, iy1, -off_side, 0.0f, off_inside, 0.0f);
    const float right_edge = edge_transition_score(rgba, width, height, stride, &h, ix1, iy0, ix1, iy1, off_side, 0.0f, -off_inside, 0.0f);

    const float top_fill = border_fill_score(rgba, width, height, stride, &h, ix0, 0.0f, ix1, iy0);
    const float bottom_fill = border_fill_score(rgba, width, height, stride, &h, ix0, iy1, ix1, oh);
    const float left_fill = border_fill_score(rgba, width, height, stride, &h, 0.0f, iy0, ix0, iy1);
    const float right_fill = border_fill_score(rgba, width, height, stride, &h, ix1, iy0, ow, iy1);

    const float edge_score = (top_edge + bottom_edge + left_edge + right_edge) * 0.25f;
    const float fill_score = (top_fill + bottom_fill + left_fill + right_fill) * 0.25f;
    const float side_fill_score = (left_fill + right_fill) * 0.5f;

    /*
     * Dense writing mainly hurts the white-fill score. For normal photos, the
     * side-border fill is especially useful for separating Instax Wide from
     * Polaroid-like square-window templates, which can otherwise produce a
     * similarly strong edge score under perspective.
     */
    const float bottom_bonus = clampf_local((bottom_fill - 0.45f) / 0.45f, 0.0f, 1.0f);
    const float score = 0.48f * edge_score +
                        0.35f * fill_score +
                        0.12f * side_fill_score +
                        0.05f * bottom_bonus;

    if (out) {
        out->type = film->type;
        out->score = score;
        out->canonical_w = ow;
        out->canonical_h = oh;
        out->image_w = iw;
        out->image_h = ih;
        out->inner_aspect = normalized_ratio(iw, ih);
        out->left_margin = left_margin;
        out->top_margin = top_margin;
        out->right_margin = right_margin;
        out->bottom_margin = bottom_margin;
        out->rotated = rotated;
        out->confidence = clampf_local((score - 0.38f) / 0.36f, 0.0f, 1.0f);
    }
    return score;
}

static float score_refined_inner_window(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_homography *h,
    float outer_w,
    float outer_h,
    float x0,
    float y0,
    float x1,
    float y1,
    float base_x0,
    float base_y0,
    float base_x1,
    float base_y1
) {
    const float iw = x1 - x0;
    const float ih = y1 - y0;
    if (iw <= 4.0f || ih <= 4.0f) {
        return -1.0e9f;
    }
    if (x0 <= 0.2f || y0 <= 0.2f || x1 >= outer_w - 0.2f || y1 >= outer_h - 0.2f) {
        return -1.0e9f;
    }

    const float off_side = clampf_local(fminf(x0, outer_w - x1) * 0.50f, 1.0f, 5.0f);
    const float off_top = clampf_local(y0 * 0.50f, 1.0f, 5.0f);
    const float off_bottom = clampf_local((outer_h - y1) * 0.35f, 1.0f, 7.0f);
    const float off_inside = clampf_local(fminf(iw, ih) * 0.035f, 1.0f, 5.0f);

    const float top_edge = edge_transition_score(rgba, width, height, stride, h, x0, y0, x1, y0, 0.0f, -off_top, 0.0f, off_inside);
    const float bottom_edge = edge_transition_score(rgba, width, height, stride, h, x0, y1, x1, y1, 0.0f, off_bottom, 0.0f, -off_inside);
    const float left_edge = edge_transition_score(rgba, width, height, stride, h, x0, y0, x0, y1, -off_side, 0.0f, off_inside, 0.0f);
    const float right_edge = edge_transition_score(rgba, width, height, stride, h, x1, y0, x1, y1, off_side, 0.0f, -off_inside, 0.0f);
    const float edge_score = (top_edge + bottom_edge + left_edge + right_edge) * 0.25f;

    const float outside_fill = (
        border_fill_score(rgba, width, height, stride, h, x0, 0.0f, x1, y0) +
        border_fill_score(rgba, width, height, stride, h, x0, y1, x1, outer_h) +
        border_fill_score(rgba, width, height, stride, h, 0.0f, y0, x0, y1) +
        border_fill_score(rgba, width, height, stride, h, x1, y0, outer_w, y1)
    ) * 0.25f;

    const float drift = fabsf(x0 - base_x0) + fabsf(y0 - base_y0) + fabsf(x1 - base_x1) + fabsf(y1 - base_y1);
    const float penalty = 0.018f * drift;

    return 0.74f * edge_score + 0.26f * outside_fill - penalty;
}

static int refine_inner_window_rect(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point corners[4],
    float outer_w,
    float outer_h,
    float base_x0,
    float base_y0,
    float base_x1,
    float base_y1,
    float *out_x0,
    float *out_y0,
    float *out_x1,
    float *out_y1
) {
    instant_homography h;
    if (!homography_from_canonical_to_source(outer_w, outer_h, corners, &h)) {
        return 0;
    }

    float x0 = base_x0;
    float y0 = base_y0;
    float x1 = base_x1;
    float y1 = base_y1;
    const float min_inner_w = fmaxf(outer_w * 0.35f, 16.0f);
    const float min_inner_h = fmaxf(outer_h * 0.35f, 16.0f);

    for (int iter = 0; iter < 3; ++iter) {
        float best = score_refined_inner_window(rgba, width, height, stride, &h, outer_w, outer_h, x0, y0, x1, y1, base_x0, base_y0, base_x1, base_y1);

        float range = clampf_local(base_x0 * 0.85f, 1.0f, 6.0f);
        for (int s = 0; s <= 12; ++s) {
            float cand = (base_x0 - range) + (2.0f * range * (float)s / 12.0f);
            cand = clampf_local(cand, 0.5f, x1 - min_inner_w);
            float score = score_refined_inner_window(rgba, width, height, stride, &h, outer_w, outer_h, cand, y0, x1, y1, base_x0, base_y0, base_x1, base_y1);
            if (score > best) { best = score; x0 = cand; }
        }

        range = clampf_local(base_y0 * 0.85f, 1.0f, 6.0f);
        for (int s = 0; s <= 12; ++s) {
            float cand = (base_y0 - range) + (2.0f * range * (float)s / 12.0f);
            cand = clampf_local(cand, 0.5f, y1 - min_inner_h);
            float score = score_refined_inner_window(rgba, width, height, stride, &h, outer_w, outer_h, x0, cand, x1, y1, base_x0, base_y0, base_x1, base_y1);
            if (score > best) { best = score; y0 = cand; }
        }

        range = clampf_local((outer_w - base_x1) * 0.85f, 1.0f, 6.0f);
        for (int s = 0; s <= 12; ++s) {
            float cand = (base_x1 - range) + (2.0f * range * (float)s / 12.0f);
            cand = clampf_local(cand, x0 + min_inner_w, outer_w - 0.5f);
            float score = score_refined_inner_window(rgba, width, height, stride, &h, outer_w, outer_h, x0, y0, cand, y1, base_x0, base_y0, base_x1, base_y1);
            if (score > best) { best = score; x1 = cand; }
        }

        range = clampf_local((outer_h - base_y1) * 0.85f, 1.0f, 8.0f);
        for (int s = 0; s <= 12; ++s) {
            float cand = (base_y1 - range) + (2.0f * range * (float)s / 12.0f);
            cand = clampf_local(cand, y0 + min_inner_h, outer_h - 0.5f);
            float score = score_refined_inner_window(rgba, width, height, stride, &h, outer_w, outer_h, x0, y0, x1, cand, base_x0, base_y0, base_x1, base_y1);
            if (score > best) { best = score; y1 = cand; }
        }
    }

    *out_x0 = x0;
    *out_y0 = y0;
    *out_x1 = x1;
    *out_y1 = y1;
    return 1;
}

static instant_template_match classify_film_by_inner_template(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point corners[4]
) {
    instant_template_match best;
    memset(&best, 0, sizeof(best));
    best.type = INSTANT_FILM_UNKNOWN;
    best.score = -1.0f;

    for (int i = 0; i < INSTANT_FILM_DIMENSIONS_COUNT; ++i) {
        for (int rot = 0; rot < 2; ++rot) {
            instant_template_match candidate;
            const float score = score_template_orientation(rgba, width, height, stride, corners, &INSTANT_FILM_DIMENSIONS[i], rot, &candidate);
            if (score > best.score) {
                best = candidate;
            }
        }
    }

    if (best.score < 0.38f) {
        best.type = INSTANT_FILM_UNKNOWN;
        best.confidence = 0.0f;
    }
    return best;
}
instant_options instant_default_options(void) {
    instant_options options;
    options.max_output_width = 1200;
    options.return_warped_image = 0;
    options.min_confidence = 0.35f;
    return options;
}

/*
 * White-border detector with line-fitted corners.
 *
 * The detector remains dependency-free C for easy Android NDK builds. It first
 * finds bright, near-white connected components, chooses the best instant-film
 * frame candidate, extracts its boundary, fits one line to each side, and uses
 * line intersections as the final skew-corrected corner estimate.
 *
 * This is still not a full Canny/Hough implementation, but it is a meaningful
 * step up from the earlier bounding-box/extreme-pixel detector: skewed photos
 * now produce geometric side lines instead of rough axis-aligned dimensions.
 */
instant_result instant_scan_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    instant_options options
) {
    if (!rgba) {
        return make_error("Input pixel buffer is null.");
    }
    if (width <= 0 || height <= 0 || stride < width * 4) {
        return make_error("Invalid image dimensions or stride.");
    }

    const int pixel_count = width * height;
    if (pixel_count <= 0) {
        return make_error("Image is too large or invalid.");
    }

    unsigned char *mask = (unsigned char *)calloc((size_t)pixel_count, sizeof(unsigned char));
    unsigned char *visited = (unsigned char *)calloc((size_t)pixel_count, sizeof(unsigned char));
    int *queue = (int *)malloc((size_t)pixel_count * sizeof(int));
    int *best_pixels = (int *)malloc((size_t)pixel_count * sizeof(int));

    if (!mask || !visited || !queue || !best_pixels) {
        free(mask);
        free(visited);
        free(queue);
        free(best_pixels);
        return make_error("Out of memory while detecting white border.");
    }

    for (int y = 0; y < height; ++y) {
        const unsigned char *row = rgba + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; ++x) {
            const unsigned char *p = row + x * 4;
            mask[y * width + x] = (unsigned char)is_white_border_pixel(p);
        }
    }

    /*
     * Repair the white-border mask before connected components. This closes
     * thin dark gaps from handwriting, date stamps, dirt, or scratches on the
     * border while keeping the original image untouched for final extraction.
     */
    if (!close_mask(mask, width, height, 2)) {
        free(mask);
        free(visited);
        free(queue);
        free(best_pixels);
        return make_error("Out of memory while repairing white-border mask.");
    }

    int best_count = 0;
    int best_pixel_count = 0;
    const int min_component_area = pixel_count / 250; /* 0.4% of input */

    for (int start = 0; start < pixel_count; ++start) {
        if (!mask[start] || visited[start]) {
            continue;
        }

        int head = 0;
        int tail = 0;
        queue[tail++] = start;
        visited[start] = 1;

        int min_x = width;
        int min_y = height;
        int max_x = 0;
        int max_y = 0;

        while (head < tail) {
            const int idx = queue[head++];
            const int x = idx % width;
            const int y = idx / width;

            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;

            const int left = x > 0 ? idx - 1 : -1;
            const int right = x + 1 < width ? idx + 1 : -1;
            const int up = y > 0 ? idx - width : -1;
            const int down = y + 1 < height ? idx + width : -1;

            if (left >= 0 && mask[left] && !visited[left]) {
                visited[left] = 1;
                queue[tail++] = left;
            }
            if (right >= 0 && mask[right] && !visited[right]) {
                visited[right] = 1;
                queue[tail++] = right;
            }
            if (up >= 0 && mask[up] && !visited[up]) {
                visited[up] = 1;
                queue[tail++] = up;
            }
            if (down >= 0 && mask[down] && !visited[down]) {
                visited[down] = 1;
                queue[tail++] = down;
            }
        }

        const int count = tail;
        const int bbox_w = max_x - min_x + 1;
        const int bbox_h = max_y - min_y + 1;
        const float bbox_ratio = normalized_ratio((float)bbox_w, (float)bbox_h);
        const float fill_ratio = (float)count / (float)(bbox_w * bbox_h);

        if (count >= min_component_area &&
            bbox_w >= width / 10 &&
            bbox_h >= height / 10 &&
            bbox_ratio >= 0.95f &&
            bbox_ratio <= 1.95f &&
            fill_ratio >= 0.08f &&
            fill_ratio <= 0.90f) {
            if (count > best_count) {
                best_count = count;
                best_pixel_count = count;
                memcpy(best_pixels, queue, (size_t)count * sizeof(int));
            }
        }
    }

    free(mask);
    free(visited);
    free(queue);

    if (best_count == 0) {
        free(best_pixels);
        return make_error("Could not find a white instant-film border. Try a darker background or stronger border contrast.");
    }

    instant_point corners[4];
    if (!estimate_corners_from_component(best_pixels, best_pixel_count, width, height, corners)) {
        free(best_pixels);
        return make_error("Found a white border, but could not fit its four sides.");
    }
    free(best_pixels);

    const float top_w = point_distance(corners[0], corners[1]);
    const float bottom_w = point_distance(corners[3], corners[2]);
    const float left_h = point_distance(corners[0], corners[3]);
    const float right_h = point_distance(corners[1], corners[2]);
    const float detected_width = (top_w + bottom_w) * 0.5f;
    const float detected_height = (left_h + right_h) * 0.5f;

    float ratio_confidence = 0.0f;
    const instant_film_type ratio_type = instant_classify_film_by_outer_ratio(
        detected_width,
        detected_height,
        &ratio_confidence
    );

    const instant_template_match template_match = classify_film_by_inner_template(
        rgba, width, height, stride, corners
    );

    instant_film_type type = ratio_type;
    float confidence = ratio_confidence;
    float output_aspect = normalized_ratio(detected_width, detected_height);
    int output_width = (int)(detected_width + 0.5f);
    int output_height = (int)(detected_height + 0.5f);
    float inner_aspect = 0.0f;

    /*
     * Projected side lengths are not perspective-invariant. For angled phone
     * photos, prefer the inner-window template score when it sees the expected
     * white border around the photo area. This fixes many cases where the
     * outer quadrilateral looks too square in the camera view.
     */
    if (template_match.type != INSTANT_FILM_UNKNOWN &&
        (ratio_type == INSTANT_FILM_UNKNOWN || ratio_confidence < 0.35f)) {
        type = template_match.type;
        confidence = template_match.confidence;
        output_aspect = normalized_ratio(template_match.canonical_w, template_match.canonical_h);
        output_width = (int)(template_match.canonical_w + 0.5f);
        output_height = (int)(template_match.canonical_h + 0.5f);
        inner_aspect = template_match.inner_aspect;
    }

    instant_result result;
    memset(&result, 0, sizeof(result));

    result.success = type != INSTANT_FILM_UNKNOWN && confidence >= options.min_confidence;
    result.film_type = type;
    result.confidence = confidence;
    result.corrected_width = output_width;
    result.corrected_height = output_height;
    result.outer_aspect = output_aspect;
    result.inner_aspect = inner_aspect;

    for (int i = 0; i < 4; ++i) {
        result.corners[i] = corners[i];
    }

    const instant_film_dimensions *film = find_film_dimensions(type);
    if (film) {
        float outer_w = film->outer_width_mm;
        float outer_h = film->outer_height_mm;
        float image_w = film->image_width_mm;
        float image_h = film->image_height_mm;
        float left_margin = film->image_left_mm;
        float top_margin = film->image_top_mm;
        float right_margin = film->image_right_mm;
        float bottom_margin = film->image_bottom_mm;

        if (template_match.type == type && template_match.score > 0.0f) {
            outer_w = template_match.canonical_w;
            outer_h = template_match.canonical_h;
            image_w = template_match.image_w;
            image_h = template_match.image_h;
            left_margin = template_match.left_margin;
            top_margin = template_match.top_margin;
            right_margin = template_match.right_margin;
            bottom_margin = template_match.bottom_margin;
        }

        {
            const int physical_landscape = corners_are_physically_landscape(corners);
            const int template_landscape = outer_w >= outer_h;
            if (physical_landscape != template_landscape) {
                rotate_template_90cw(
                    &outer_w,
                    &outer_h,
                    &image_w,
                    &image_h,
                    &left_margin,
                    &top_margin,
                    &right_margin,
                    &bottom_margin
                );
            }
        }

        result.corrected_width = (int)(outer_w + 0.5f);
        result.corrected_height = (int)(outer_h + 0.5f);
        result.inner_corrected_width = (int)(image_w + 0.5f);
        result.inner_corrected_height = (int)(image_h + 0.5f);
        result.outer_aspect = normalized_ratio(outer_w, outer_h);
        result.inner_aspect = normalized_ratio(image_w, image_h);

        instant_homography outer_to_source;
        if (homography_from_canonical_to_source(outer_w, outer_h, corners, &outer_to_source)) {
            float x0 = left_margin;
            float y0 = top_margin;
            float x1 = outer_w - right_margin;
            float y1 = outer_h - bottom_margin;

            refine_inner_window_rect(
                rgba, width, height, stride,
                corners,
                outer_w, outer_h,
                x0, y0, x1, y1,
                &x0, &y0, &x1, &y1
            );

            {
                const float side_trim = clampf_local(fminf(x0, outer_w - x1) * 0.22f, 0.40f, 1.25f);
                const float top_trim = clampf_local(y0 * 0.35f, 0.45f, 1.90f);
                const float bottom_trim = clampf_local((outer_h - y1) * 0.12f, 0.40f, 2.20f);
                x0 += side_trim;
                x1 -= side_trim;
                y0 += top_trim;
                y1 -= bottom_trim;
            }

            result.inner_corrected_width = (int)((x1 - x0) + 0.5f);
            result.inner_corrected_height = (int)((y1 - y0) + 0.5f);
            result.inner_aspect = normalized_ratio(x1 - x0, y1 - y0);
            result.inner_corners[0] = homography_map_point(&outer_to_source, x0, y0);
            result.inner_corners[1] = homography_map_point(&outer_to_source, x1, y0);
            result.inner_corners[2] = homography_map_point(&outer_to_source, x1, y1);
            result.inner_corners[3] = homography_map_point(&outer_to_source, x0, y1);
        }
    }

    if (!result.success) {
        if (type == INSTANT_FILM_UNKNOWN) {
            strncpy(result.error, "Found a white border, but its ratio does not match a known film type.", sizeof(result.error) - 1);
        } else {
            strncpy(result.error, "Found a likely film border, but classification confidence is below the configured threshold.", sizeof(result.error) - 1);
        }
    }

    (void)INSTANT_LINE_VERTICAL_EPS;
    return result;
}

static unsigned char clamp_u8_from_float(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)(v + 0.5f);
}

static void sample_bilinear_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    float fx,
    float fy,
    unsigned char out[4]
) {
    if (fx < 0.0f || fy < 0.0f || fx > (float)(width - 1) || fy > (float)(height - 1)) {
        out[0] = out[1] = out[2] = 0;
        out[3] = 255;
        return;
    }

    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    if (x1 >= width) x1 = width - 1;
    if (y1 >= height) y1 = height - 1;

    const float tx = fx - (float)x0;
    const float ty = fy - (float)y0;
    const unsigned char *p00 = rgba + (size_t)y0 * (size_t)stride + (size_t)x0 * 4u;
    const unsigned char *p10 = rgba + (size_t)y0 * (size_t)stride + (size_t)x1 * 4u;
    const unsigned char *p01 = rgba + (size_t)y1 * (size_t)stride + (size_t)x0 * 4u;
    const unsigned char *p11 = rgba + (size_t)y1 * (size_t)stride + (size_t)x1 * 4u;

    for (int c = 0; c < 4; ++c) {
        const float v0 = (float)p00[c] * (1.0f - tx) + (float)p10[c] * tx;
        const float v1 = (float)p01[c] * (1.0f - tx) + (float)p11[c] * tx;
        out[c] = clamp_u8_from_float(v0 * (1.0f - ty) + v1 * ty);
    }
}

int instant_extract_quad_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point corners[4],
    int output_width,
    int output_height,
    unsigned char *out_rgba,
    int out_stride
) {
    if (!rgba || !corners || !out_rgba) {
        return 0;
    }
    if (width <= 0 || height <= 0 || stride < width * 4 ||
        output_width <= 0 || output_height <= 0 || out_stride < output_width * 4) {
        return 0;
    }

    instant_homography h;
    if (!homography_from_canonical_to_source((float)(output_width - 1), (float)(output_height - 1), corners, &h)) {
        return 0;
    }

    for (int y = 0; y < output_height; ++y) {
        unsigned char *dst = out_rgba + (size_t)y * (size_t)out_stride;
        for (int x = 0; x < output_width; ++x) {
            instant_point src = homography_map_point(&h, (float)x, (float)y);
            sample_bilinear_rgba(rgba, width, height, stride, src.x, src.y, dst + (size_t)x * 4u);
        }
    }

    return 1;
}

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
) {
    return instant_extract_quad_rgba(rgba, width, height, stride, corners, output_width, output_height, out_rgba, out_stride);
}

int instant_extract_inner_rgba(
    const unsigned char *rgba,
    int width,
    int height,
    int stride,
    const instant_point inner_corners[4],
    int output_width,
    int output_height,
    unsigned char *out_rgba,
    int out_stride
) {
    return instant_extract_quad_rgba(rgba, width, height, stride, inner_corners, output_width, output_height, out_rgba, out_stride);
}
