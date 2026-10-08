#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include <stdint.h>

#define BOOT_SPLASH_BACKGROUND 0x00121943u

/* XRGB8888, stride in pixels. No allocation or platform-specific calls. */
void boot_splash_render(uint32_t *pixels, int stride, int width, int height);

#endif
