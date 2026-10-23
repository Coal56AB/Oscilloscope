#ifndef OSCILL_LINUX_STATUS_KEY_H
#define OSCILL_LINUX_STATUS_KEY_H

typedef struct {
    int fd;
} LinuxStatusKey;

/* Board K1 is PS MIO0. Its GPIO line is independent of the OLED/PL registers. */
int linux_status_key_open(LinuxStatusKey *key);
/* Returns 1 pressed, 0 released, -1 on a device/read error. */
int linux_status_key_read(const LinuxStatusKey *key);
void linux_status_key_close(LinuxStatusKey *key);
#endif
