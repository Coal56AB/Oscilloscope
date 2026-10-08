#ifndef ROUNDED_BOX_H
#define ROUNDED_BOX_H

#include <stdint.h>

#define ROUNDED_BOX_MAX_RADIUS 32

/* Save only the corners. Contents are painted normally, then masked once. */
typedef struct {
    uint32_t underlay[4][ROUNDED_BOX_MAX_RADIUS * ROUNDED_BOX_MAX_RADIUS];
    uint32_t *pixels;
    int stride, surface_width, surface_height;
    int x, y, width, height, radius;
    uint32_t border;
    int border_alpha;
} RoundedBox;

void rounded_box_begin(RoundedBox *box, uint32_t *pixels, int stride,
                       int surface_width, int surface_height,
                       int x, int y, int width, int height, int radius);
void rounded_box_end(const RoundedBox *box);

#endif
