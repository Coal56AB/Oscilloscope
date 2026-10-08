#include "boot_splash.h"
#include "boot_logo_data.h"
#include <stddef.h>

void boot_splash_render(uint32_t *pixels, int stride, int width, int height)
{
    size_t input = 0;
    int x, y, position = 0;
    int left = (width - BOOT_LOGO_WIDTH) / 2;
    int top = (height - BOOT_LOGO_HEIGHT) / 2;
    if (!pixels || width <= 0 || height <= 0 || stride < width) return;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            pixels[(size_t)y * stride + x] = BOOT_SPLASH_BACKGROUND;
    while (input < sizeof(boot_logo_data)) {
        unsigned control = boot_logo_data[input++];
        unsigned count = (control & 127) + 1;
        unsigned repeated = control & 128 ? boot_logo_data[input++] : 0;
        unsigned i;
        for (i = 0; i < count; ++i, ++position) {
            unsigned alpha = control & 128 ? repeated : boot_logo_data[input++];
            uint32_t color = 0;
            int shift;
            x = left + position % BOOT_LOGO_WIDTH;
            y = top + position / BOOT_LOGO_WIDTH;
            if ((unsigned)x >= (unsigned)width || (unsigned)y >= (unsigned)height)
                continue;
            for (shift = 0; shift <= 16; shift += 8) {
                unsigned base = (BOOT_SPLASH_BACKGROUND >> shift) & 255;
                color |= (base + ((255 - base) * alpha + 127) / 255) << shift;
            }
            pixels[(size_t)y * stride + x] = color;
        }
    }
}
