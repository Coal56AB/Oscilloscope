#ifndef LINUX_SERIAL_H
#define LINUX_SERIAL_H
#include <termios.h>
#include <stddef.h>

int linux_serial_speed(unsigned baud, speed_t *speed);
int linux_serial_get_baud(const char *path, unsigned *baud);
int linux_serial_set_baud(const char *path, unsigned baud);
/* Enumerate USB serial character devices in lexical order, wrapping after last. */
int linux_serial_next_usb(const char *directory, const char *previous, char *path, size_t capacity);

#endif
