#ifndef IMAGE_H
#define IMAGE_H
/*
 * image.h — floating-point image buffer with PPM (P6) output.
 *
 * Linear HDR values are stored per pixel; `img_write_ppm` applies a
 * sqrt (gamma ~2.0) tone map and clamps to 8-bit RGB.
 */

#include <stdio.h>
#include <stdlib.h>
#include "vec.h"

typedef struct {
    int w, h;
    vec3 *data;
} image;

static inline image img_new(int w, int h) {
    image img = {w, h, (vec3*)calloc(w * h, sizeof(vec3))};
    return img;
}

static inline void img_set(image img, int x, int y, vec3 c) {
    if (x >= 0 && x < img.w && y >= 0 && y < img.h)
        img.data[y * img.w + x] = c;
}

static inline vec3 img_get(image img, int x, int y) {
    if (x >= 0 && x < img.w && y >= 0 && y < img.h)
        return img.data[y * img.w + x];
    return v3(0, 0, 0);
}

static inline void img_write_ppm(image img, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    for (int i = 0; i < img.w * img.h; i++) {
        vec3 c = img.data[i];
        unsigned char r = (unsigned char)(255.0f * clampf(sqrtf(c.x), 0, 1));
        unsigned char g = (unsigned char)(255.0f * clampf(sqrtf(c.y), 0, 1));
        unsigned char b = (unsigned char)(255.0f * clampf(sqrtf(c.z), 0, 1));
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

static inline void img_free(image img) {
    free(img.data);
}

#endif
