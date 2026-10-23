#include "linux_serial.h"
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static const struct { unsigned baud; speed_t speed; } speeds[] = {
    {9600, B9600}, {19200, B19200}, {38400, B38400}, {57600, B57600},
    {115200, B115200}, {230400, B230400}, {460800, B460800},
    {921600, B921600}, {1000000, B1000000}, {2000000, B2000000}
};

int linux_serial_speed(unsigned baud, speed_t *speed)
{
    unsigned i;
    for (i = 0; i < sizeof(speeds) / sizeof(speeds[0]); ++i)
        if (speeds[i].baud == baud) { *speed = speeds[i].speed; return 1; }
    return 0;
}

static int configure(const char *path, unsigned *baud, int write_speed)
{
    struct termios settings;
    unsigned i;
    int ok = 0;
    int fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return 0;
    if (tcgetattr(fd, &settings) == 0) {
        for (i = 0; i < sizeof(speeds) / sizeof(speeds[0]); ++i) {
            if (write_speed && speeds[i].baud == *baud) {
                ok = cfsetispeed(&settings, speeds[i].speed) == 0 &&
                     cfsetospeed(&settings, speeds[i].speed) == 0 &&
                     tcsetattr(fd, TCSADRAIN, &settings) == 0;
                break;
            }
            if (!write_speed && cfgetospeed(&settings) == speeds[i].speed) {
                *baud = speeds[i].baud;
                ok = 1;
                break;
            }
        }
    }
    close(fd);
    return ok;
}

int linux_serial_get_baud(const char *path, unsigned *baud)
{
    return path && baud && configure(path, baud, 0);
}

int linux_serial_set_baud(const char *path, unsigned baud)
{
    return path && configure(path, &baud, 1);
}

int linux_serial_next_usb(const char *directory, const char *previous, char *path, size_t capacity)
{
    DIR *dir;
    struct dirent *entry;
    char first[256] = "", next[256] = "";
    const char *after;
    if (!directory || !path || !capacity) return 0;
    path[0] = '\0';
    after = previous ? strrchr(previous, '/') : NULL;
    after = after ? after + 1 : (previous ? previous : "");
    dir = opendir(directory);
    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name, *digit;
        char candidate[512];
        struct stat status;
        int length;
        if (strncmp(name, "ttyUSB", 6) && strncmp(name, "ttyACM", 6)) continue;
        digit = name + 6;
        if (!*digit) continue;
        while (*digit >= '0' && *digit <= '9') ++digit;
        if (*digit) continue;
        length = snprintf(candidate, sizeof(candidate), "%s/%s", directory, name);
        if (length < 0 || (size_t)length >= sizeof(candidate) || (size_t)length >= capacity ||
            strlen(name) >= sizeof(first) || stat(candidate, &status) || !S_ISCHR(status.st_mode))
            continue;
        if (!first[0] || strcmp(name, first) < 0) strcpy(first, name);
        if (strcmp(name, after) > 0 && (!next[0] || strcmp(name, next) < 0)) strcpy(next, name);
    }
    closedir(dir);
    if (!first[0]) return 0;
    snprintf(path, capacity, "%s/%s", directory, next[0] ? next : first);
    return 1;
}
