#ifndef LINUX_TOUCH_H
#define LINUX_TOUCH_H
typedef struct LinuxTouch LinuxTouch;
/* Device path or "auto"; Type B devices must provide at least two slots. */
LinuxTouch *linux_touch_open(const char *device, unsigned rotation);
void linux_touch_close(LinuxTouch *touch);
/* Pushes SDL finger events; -1 means the device must be reopened. */
int linux_touch_pump(LinuxTouch *touch);
#endif
