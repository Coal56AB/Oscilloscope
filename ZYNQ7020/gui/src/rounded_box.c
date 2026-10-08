#include "rounded_box.h"
#include <stddef.h>

/* Coverage is measured in screen pixels, independently of stripe thickness. */
static uint8_t coverage[ROUNDED_BOX_MAX_RADIUS + 1]
                       [ROUNDED_BOX_MAX_RADIUS][ROUNDED_BOX_MAX_RADIUS];
static uint8_t ready[ROUNDED_BOX_MAX_RADIUS + 1];

static void prepare(int radius)
{
    int x, y, sx, sy;
    if (radius <= 0 || ready[radius]) return;
    for (y = 0; y < radius; ++y)
        for (x = 0; x < radius; ++x) {
            int count = 0;
            for (sy = 0; sy < 8; ++sy)
                for (sx = 0; sx < 8; ++sx) {
                    int dx = x * 16 + sx * 2 + 1 - radius * 16;
                    int dy = y * 16 + sy * 2 + 1 - radius * 16;
                    count += dx * dx + dy * dy <= radius * radius * 256;
                }
            coverage[radius][y][x] = (uint8_t)((count * 255 + 32) / 64);
        }
    ready[radius] = 1;
}

static int inside(const RoundedBox *box, int corner, int x, int y, size_t *offset)
{
    int px = box->x + ((corner & 1) ? box->width - 1 - x : x);
    int py = box->y + ((corner & 2) ? box->height - 1 - y : y);
    if ((unsigned)px >= (unsigned)box->surface_width ||
        (unsigned)py >= (unsigned)box->surface_height) return 0;
    *offset = (size_t)py * box->stride + px;
    return 1;
}

void rounded_box_begin(RoundedBox *box, uint32_t *pixels, int stride,
                       int surface_width, int surface_height,
                       int x, int y, int width, int height, int radius)
{
    int corner, xx, yy;
    box->pixels = pixels;
    box->stride = stride;
    box->surface_width = surface_width;
    box->surface_height = surface_height;
    box->x = x; box->y = y; box->width = width; box->height = height;
    if (radius > ROUNDED_BOX_MAX_RADIUS) radius = ROUNDED_BOX_MAX_RADIUS;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    if (radius < 0) radius = 0;
    box->radius = radius;
    box->border = 0;
    box->border_alpha = 0;
    prepare(radius);
    prepare(radius - 1);
    for (corner = 0; corner < 4; ++corner)
        for (yy = 0; yy < radius; ++yy)
            for (xx = 0; xx < radius; ++xx) {
                size_t offset;
                if (inside(box, corner, xx, yy, &offset))
                    box->underlay[corner][yy * radius + xx] = pixels[offset];
            }
}

void rounded_box_end(const RoundedBox *box)
{
    int corner, x, y, radius = box->radius;
    for (corner = 0; corner < 4; ++corner)
        for (y = 0; y < radius; ++y)
            for (x = 0; x < radius; ++x) {
                size_t offset;
                int outer = coverage[radius][y][x], inner, edge, shift;
                uint32_t result = 0, content, base;
                if (!inside(box, corner, x, y, &offset)) continue;
                inner = !x || !y ? 0 : coverage[radius - 1][y - 1][x - 1];
                edge = box->border_alpha ? outer - inner : 0;
                content = box->pixels[offset];
                base = box->underlay[corner][y * radius + x];
                for (shift = 0; shift <= 16; shift += 8) {
                    unsigned border_component = (((box->border >> shift) & 255) * box->border_alpha +
                        ((base >> shift) & 255) * (255 - box->border_alpha) + 127) / 255;
                    unsigned component = (((base >> shift) & 255) * (255 - outer) +
                        ((content >> shift) & 255) * (outer - edge) +
                        border_component * edge + 127) / 255;
                    result |= component << shift;
                }
                box->pixels[offset] = result;
            }
}
