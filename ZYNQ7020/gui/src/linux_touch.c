#define _POSIX_C_SOURCE 200809L
#include "linux_touch.h"
#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
enum { TOUCH_SLOTS = 32 };
typedef struct {
    int id, previous, x, y, last_x, last_y;
} Contact;
struct LinuxTouch {
    int fd, slot, count, dropped;
    unsigned rotation;
    struct input_absinfo x, y;
    Contact contacts[TOUCH_SLOTS];
};
static void send_contact(LinuxTouch *t, int type, int id, int raw_x, int raw_y)
{
    SDL_Event event;
    float x = (float)(raw_x - t->x.minimum) / (t->x.maximum - t->x.minimum);
    float y = (float)(raw_y - t->y.minimum) / (t->y.maximum - t->y.minimum), old_x = x;
    if (x < 0)
        x = 0;
    if (x > 1)
        x = 1;
    if (y < 0)
        y = 0;
    if (y > 1)
        y = 1;
    old_x = x;
    if (t->rotation == 90) {
        x = 1 - y;
        y = old_x;
    } else if (t->rotation == 180) {
        x = 1 - x;
        y = 1 - y;
    } else if (t->rotation == 270) {
        x = y;
        y = 1 - old_x;
    }
    SDL_zero(event);
    event.type = type;
    event.tfinger.timestamp = SDL_GetTicks();
    event.tfinger.touchId = 1;
    event.tfinger.fingerId = id;
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = type == SDL_FINGERUP ? 0 : 1;
    SDL_PushEvent(&event);
}
static int report(LinuxTouch *t)
{
    int i, events = 0;
    for (i = 0; i < t->count; ++i) {
        Contact *c = &t->contacts[i];
        if (c->id != c->previous) {
            if (c->previous >= 0) {
                send_contact(t, SDL_FINGERUP, c->previous, c->last_x, c->last_y);
                ++events;
            }
            if (c->id >= 0) {
                send_contact(t, SDL_FINGERDOWN, c->id, c->x, c->y);
                ++events;
            }
        } else if (c->id >= 0 && (c->x != c->last_x || c->y != c->last_y)) {
            send_contact(t, SDL_FINGERMOTION, c->id, c->x, c->y);
            ++events;
        }
        c->previous = c->id;
        c->last_x = c->x;
        c->last_y = c->y;
    }
    return events;
}
LinuxTouch *linux_touch_open(const char *device, unsigned rotation)
{
    if (device && strcmp(device, "auto") == 0) {
        DIR *directory = opendir("/dev/input");
        struct dirent *entry;
        LinuxTouch *found = NULL;
        if (!directory)
            return NULL;
        while ((entry = readdir(directory)) != NULL) {
            char path[512];
            if (strncmp(entry->d_name, "event", 5) != 0)
                continue;
            if (snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name) >= (int)sizeof(path))
                continue;
            found = linux_touch_open(path, rotation);
            if (found) {
                fprintf(stderr, "Oscill touch: %s\n", path);
                break;
            }
        }
        closedir(directory);
        return found;
    }
    LinuxTouch *t = calloc(1, sizeof(*t));
    struct input_absinfo slots, ids;
    int i;
    if (!t)
        return NULL;
    t->fd = -1;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        goto fail;
    t->rotation = rotation;
    t->fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (t->fd < 0 || ioctl(t->fd, EVIOCGABS(ABS_MT_SLOT), &slots) < 0 || slots.minimum != 0 ||
        slots.maximum >= TOUCH_SLOTS || ioctl(t->fd, EVIOCGABS(ABS_MT_TRACKING_ID), &ids) < 0 ||
        ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_X), &t->x) < 0 ||
        ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_Y), &t->y) < 0 || t->x.maximum <= t->x.minimum ||
        t->y.maximum <= t->y.minimum)
        goto fail;
    t->count = slots.maximum + 1;
    if (t->count < 2)
        goto fail;
    for (i = 0; i < t->count; ++i)
        t->contacts[i].id = t->contacts[i].previous = -1;
    return t;
fail:
    linux_touch_close(t);
    return NULL;
}
static int recover(LinuxTouch *t)
{
    int values[TOUCH_SLOTS + 1], i;
    values[0] = ABS_MT_TRACKING_ID;
    if (ioctl(t->fd, EVIOCGMTSLOTS(sizeof(int) * (t->count + 1)), values) < 0)
        return 0;
    for (i = 0; i < t->count; ++i)
        t->contacts[i].id = values[i + 1];
    values[0] = ABS_MT_POSITION_X;
    if (ioctl(t->fd, EVIOCGMTSLOTS(sizeof(int) * (t->count + 1)), values) < 0)
        return 0;
    for (i = 0; i < t->count; ++i)
        t->contacts[i].x = values[i + 1];
    values[0] = ABS_MT_POSITION_Y;
    if (ioctl(t->fd, EVIOCGMTSLOTS(sizeof(int) * (t->count + 1)), values) < 0)
        return 0;
    for (i = 0; i < t->count; ++i)
        t->contacts[i].y = values[i + 1];
    return 1;
}
int linux_touch_pump(LinuxTouch *t)
{
    struct input_event input[128];
    ssize_t bytes;
    size_t i;
    int events = 0;
    if (!t)
        return 0;
    bytes = read(t->fd, input, sizeof(input));
    if (bytes < 0)
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    if (!bytes)
        return -1;
    if (bytes % sizeof(*input))
        return -1;
    for (i = 0; i < (size_t)bytes / sizeof(*input); ++i) {
        struct input_event *e = &input[i];
        if (e->type == EV_SYN && e->code == SYN_DROPPED) {
            int j;
            for (j = 0; j < t->count; ++j)
                t->contacts[j].id = -1;
            events += report(t);
            t->dropped = 1;
        } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
            if (t->dropped) {
                if (!recover(t))
                    return -1;
                t->dropped = 0;
            }
            events += report(t);
        } else if (!t->dropped && e->type == EV_ABS) {
            if (e->code == ABS_MT_SLOT) {
                t->slot = e->value;
                if (t->slot < 0 || t->slot >= t->count)
                    return -1;
            } else if (e->code == ABS_MT_TRACKING_ID)
                t->contacts[t->slot].id = e->value;
            else if (e->code == ABS_MT_POSITION_X)
                t->contacts[t->slot].x = e->value;
            else if (e->code == ABS_MT_POSITION_Y)
                t->contacts[t->slot].y = e->value;
        }
    }
    return events;
}
void linux_touch_close(LinuxTouch *t)
{
    if (t) {
        if (t->fd >= 0)
            close(t->fd);
        free(t);
    }
}
