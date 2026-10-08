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
    int fd, slot, count, dropped, multitouch, ready;
    char path[512];
    LinuxTouchStats stats;
    unsigned rotation;
    struct input_absinfo x, y;
    Contact contacts[TOUCH_SLOTS];
};
static void send_contact(LinuxTouch *t, int type, int id, int raw_x, int raw_y)
{
    SDL_Event event;
    float x = (float)(((double)raw_x - t->x.minimum) / ((double)t->x.maximum - t->x.minimum));
    float y = (float)(((double)raw_y - t->y.minimum) / ((double)t->y.maximum - t->y.minimum)), old_x = x;
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
    t->stats.reports++;
    t->stats.events += events;
    return events;
}
static int recover(LinuxTouch *t);
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
    struct input_absinfo slots;
    unsigned char keys[KEY_MAX / 8 + 1] = {0};
    unsigned char props[INPUT_PROP_MAX / 8 + 1] = {0};
    char name[128] = {0};
    int i, direct, pointer, button;
    if (!t)
        return NULL;
    t->fd = -1;
    if (!device || (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270))
        goto fail;
    t->rotation = rotation;
    t->fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (t->fd < 0)
        goto fail;
    ioctl(t->fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
    ioctl(t->fd, EVIOCGPROP(sizeof(props)), props);
    direct = props[INPUT_PROP_DIRECT / 8] & (1U << (INPUT_PROP_DIRECT % 8));
    pointer = props[INPUT_PROP_POINTER / 8] & (1U << (INPUT_PROP_POINTER % 8));
    button = keys[BTN_TOUCH / 8] & (1U << (BTN_TOUCH % 8));
    if ((!direct && !button) || (pointer && !direct))
        goto fail;
    t->multitouch = ioctl(t->fd, EVIOCGABS(ABS_MT_SLOT), &slots) == 0;
    if (t->multitouch) {
        if (slots.minimum != 0 || slots.maximum < 0 || slots.maximum >= TOUCH_SLOTS)
            goto fail;
        t->slot = slots.value >= 0 && slots.value <= slots.maximum ? slots.value : -1;
        t->count = slots.maximum - slots.minimum + 1;
    } else {
        if (!button)
            goto fail;
        t->count = 1;
    }
    if (ioctl(t->fd, EVIOCGABS(t->multitouch ? ABS_MT_POSITION_X : ABS_X), &t->x) < 0 ||
        ioctl(t->fd, EVIOCGABS(t->multitouch ? ABS_MT_POSITION_Y : ABS_Y), &t->y) < 0 ||
        t->x.maximum <= t->x.minimum || t->y.maximum <= t->y.minimum)
        goto fail;
    for (i = 0; i < t->count; ++i)
        t->contacts[i].id = t->contacts[i].previous = -1;
    snprintf(t->path, sizeof(t->path), "%s", device);
    ioctl(t->fd, EVIOCGNAME(sizeof(name)), name);
    name[sizeof(name) - 1] = 0;
    if (!recover(t))
        goto fail;
    t->ready = 1;
    report(t);
    fprintf(stderr, "Oscill touch connected: %s name=%s mode=%s slots=%d range=%d..%d,%d..%d\n",
            device, name, t->multitouch ? "MT-B" : "single", t->count,
            t->x.minimum, t->x.maximum, t->y.minimum, t->y.maximum);
    return t;
fail:
    linux_touch_close(t);
    return NULL;
}
static int recover(LinuxTouch *t)
{
    struct input_absinfo slots;
    if (!t->multitouch) {
        unsigned char keys[KEY_MAX / 8 + 1] = {0};
        if (ioctl(t->fd, EVIOCGKEY(sizeof(keys)), keys) < 0 ||
            ioctl(t->fd, EVIOCGABS(ABS_X), &t->x) < 0 ||
            ioctl(t->fd, EVIOCGABS(ABS_Y), &t->y) < 0)
            return 0;
        t->contacts[0].id = (keys[BTN_TOUCH / 8] & (1U << (BTN_TOUCH % 8))) ? 0 : -1;
        t->contacts[0].x = t->x.value;
        t->contacts[0].y = t->y.value;
        return 1;
    }
    if (ioctl(t->fd, EVIOCGABS(ABS_MT_SLOT), &slots) < 0)
        return 0;
    t->slot = slots.value >= 0 && slots.value < t->count ? slots.value : -1;
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
static int input_event(LinuxTouch *t, const struct input_event *e)
{
    int events = 0;
    if (e->type == EV_SYN && e->code == SYN_DROPPED) {
        int j;
        for (j = 0; j < t->count; ++j)
            t->contacts[j].id = -1;
        events += report(t);
        t->dropped = 1;
        t->stats.overflows++;
        fprintf(stderr, "Oscill touch overflow: %s; resynchronizing\n", t->path);
    } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
        if (t->dropped) {
            if (!recover(t))
                return -1;
            t->dropped = 0;
        }
        events += report(t);
    } else if (!t->dropped) {
        if (t->multitouch && e->type == EV_ABS && e->code == ABS_MT_SLOT) {
            t->slot = e->value >= 0 && e->value < t->count ? e->value : -1;
        } else if (t->slot >= 0 && t->slot < t->count) {
            Contact *c = &t->contacts[t->slot];
            if (t->multitouch && e->type == EV_ABS) {
                if (e->code == ABS_MT_TRACKING_ID) c->id = e->value;
                else if (e->code == ABS_MT_POSITION_X) c->x = e->value;
                else if (e->code == ABS_MT_POSITION_Y) c->y = e->value;
            } else if (!t->multitouch) {
                if (e->type == EV_KEY && e->code == BTN_TOUCH) c->id = e->value ? 0 : -1;
                else if (e->type == EV_ABS && e->code == ABS_X) c->x = e->value;
                else if (e->type == EV_ABS && e->code == ABS_Y) c->y = e->value;
            }
        }
    }
    return events;
}
int linux_touch_pump(LinuxTouch *t)
{
    struct input_event input[128];
    int events = 0, batch;
    if (!t)
        return 0;
    /* Drain queued reports without letting a busy device starve rendering. */
    for (batch = 0; batch < 16; ++batch) {
        ssize_t bytes = read(t->fd, input, sizeof(input));
        size_t i;
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR))
            return events;
        if (bytes <= 0 || bytes % sizeof(*input)) {
            int error = bytes < 0 ? errno : EIO;
            fprintf(stderr, "Oscill touch disconnected: %s: %s\n", t->path, strerror(error));
            return -1;
        }
        for (i = 0; i < (size_t)bytes / sizeof(*input); ++i) {
            int result = input_event(t, &input[i]);
            if (result < 0) {
                fprintf(stderr, "Oscill touch resync failed: %s: %s\n", t->path, strerror(errno));
                return -1;
            }
            events += result;
        }
    }
    return events;
}
void linux_touch_stats(const LinuxTouch *t, LinuxTouchStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (t) *stats = t->stats;
}
void linux_touch_close(LinuxTouch *t)
{
    if (t) {
        if (t->ready) {
            int i;
            for (i = 0; i < t->count; ++i) t->contacts[i].id = -1;
            report(t);
        }
        if (t->fd >= 0)
            close(t->fd);
        free(t);
    }
}
