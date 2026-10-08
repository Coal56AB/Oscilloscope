#ifndef LINUX_TOUCH_H
#define LINUX_TOUCH_H
#include <stdint.h>
typedef struct LinuxTouch LinuxTouch;
typedef struct { uint64_t reports, events, overflows; } LinuxTouchStats;
/* Device path or "auto"; single-touch and Type B touchscreen devices. */
LinuxTouch *linux_touch_open(const char *device, unsigned rotation);
void linux_touch_close(LinuxTouch *touch);
/* Pushes SDL finger events; -1 means the device must be reopened. */
int linux_touch_pump(LinuxTouch *touch);
void linux_touch_stats(const LinuxTouch *touch, LinuxTouchStats *stats);
#endif
