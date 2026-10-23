#ifndef OSCILL_LINUX_OLED_H
#define OSCILL_LINUX_OLED_H
#include "oled.h"
typedef struct {
    volatile uint32_t *registers;
    int fd;
} LinuxOled;
int linux_oled_open(LinuxOled *device, Oled *oled);
void linux_oled_close(LinuxOled *device);
#endif
