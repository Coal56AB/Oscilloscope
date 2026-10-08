#ifndef LINUX_DISPLAY_H
#define LINUX_DISPLAY_H
#include <stdint.h>
typedef struct LinuxDisplay LinuxDisplay;
LinuxDisplay *linux_display_open(const char *device, int width, int height);
LinuxDisplay *linux_display_open_framebuffer(const char *device, int width, int height);
void linux_display_close(LinuxDisplay *display);
/* 1 submitted, 0 previous flip pending, -1 error. */
int linux_display_present(LinuxDisplay *display, const uint32_t *pixels);
#endif
