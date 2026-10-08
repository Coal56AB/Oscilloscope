#define _POSIX_C_SOURCE 200809L
#include "../linux_touch.h"
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static int slot_count, pointer_device, disconnected, current_slot, active[32], xs[32], ys[32];
static struct input_event queue[4096];
static size_t queued, consumed;

int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    CHECK(strcmp(path, "/dev/input/test") == 0);
    return 42;
}
int __wrap_close(int fd) { CHECK(fd == 42); return 0; }
ssize_t __wrap_read(int fd, void *buffer, size_t bytes)
{
    size_t n = queued - consumed;
    CHECK(fd == 42);
    if (!n) { errno = disconnected ? ENODEV : EAGAIN; return -1; }
    if (n > bytes / sizeof(*queue)) n = bytes / sizeof(*queue);
    memcpy(buffer, queue + consumed, n * sizeof(*queue));
    consumed += n;
    return (ssize_t)(n * sizeof(*queue));
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *out;
    int code = (int)_IOC_NR(request), selector = 0;
    CHECK(fd == 42);
    va_start(args, request); out = va_arg(args, void *); va_end(args);
    if (code == _IOC_NR(EVIOCGMTSLOTS(1))) selector = *(int *)out;
    memset(out, 0, _IOC_SIZE(request));
    if (code == _IOC_NR(EVIOCGBIT(EV_KEY, 1)) || code == _IOC_NR(EVIOCGKEY(1))) {
        if (code != _IOC_NR(EVIOCGKEY(1)) || active[0] >= 0)
            ((unsigned char *)out)[BTN_TOUCH / 8] |= 1U << (BTN_TOUCH % 8);
    } else if (code == _IOC_NR(EVIOCGPROP(1))) {
        int prop = pointer_device ? INPUT_PROP_POINTER : INPUT_PROP_DIRECT;
        ((unsigned char *)out)[prop / 8] |= 1U << (prop % 8);
    } else if (code == _IOC_NR(EVIOCGNAME(1))) {
        snprintf(out, _IOC_SIZE(request), "test touchscreen");
    } else if (code == _IOC_NR(EVIOCGMTSLOTS(1))) {
        int *values = out, i;
        values[0] = selector;
        for (i = 0; i < slot_count; ++i)
            values[i + 1] = selector == ABS_MT_TRACKING_ID ? active[i] :
                            selector == ABS_MT_POSITION_X ? xs[i] : ys[i];
    } else if (code >= _IOC_NR(EVIOCGABS(0)) && code <= _IOC_NR(EVIOCGABS(ABS_MAX))) {
        int axis = code - _IOC_NR(EVIOCGABS(0));
        struct input_absinfo *a = out;
        if (axis == ABS_MT_SLOT) {
            if (!slot_count) { errno = EINVAL; return -1; }
            a->maximum = slot_count - 1; a->value = current_slot;
        } else {
            a->maximum = 1000;
            a->value = (axis == ABS_X || axis == ABS_MT_POSITION_X) ? xs[0] : ys[0];
        }
    } else CHECK(0);
    return 0;
}
static void reset(int slots)
{
    SDL_Event event;
    int i;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {}
    slot_count = slots; pointer_device = disconnected = current_slot = 0;
    queued = consumed = 0;
    for (i = 0; i < 32; ++i) { active[i] = -1; xs[i] = ys[i] = 0; }
}
static void emit(unsigned short type, unsigned short code, int value)
{
    CHECK(queued < sizeof(queue) / sizeof(*queue));
    memset(&queue[queued], 0, sizeof(*queue));
    queue[queued].type = type; queue[queued].code = code; queue[queued++].value = value;
}
static void finger(Uint32 type, int id, float x, float y)
{
    SDL_Event event;
    CHECK(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1);
    CHECK(event.type == type && event.tfinger.fingerId == id);
    CHECK(fabsf(event.tfinger.x - x) < 0.001f && fabsf(event.tfinger.y - y) < 0.001f);
}
static void empty(void) { SDL_Event e; CHECK(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 0); }
int main(void)
{
    LinuxTouch *touch;
    LinuxTouchStats stats;
    int i;
    CHECK(SDL_Init(SDL_INIT_EVENTS) == 0);
    reset(0);
    touch = linux_touch_open("/dev/input/test", 0); CHECK(touch);
    emit(EV_ABS, ABS_X, 200); emit(EV_ABS, ABS_Y, 700);
    emit(EV_KEY, BTN_TOUCH, 1); emit(EV_SYN, SYN_REPORT, 0);
    CHECK(linux_touch_pump(touch) == 1); finger(SDL_FINGERDOWN, 0, .2f, .7f);
    emit(EV_ABS, ABS_X, 300); emit(EV_SYN, SYN_REPORT, 0);
    emit(EV_KEY, BTN_TOUCH, 0); emit(EV_SYN, SYN_REPORT, 0);
    CHECK(linux_touch_pump(touch) == 2);
    finger(SDL_FINGERMOTION, 0, .3f, .7f); finger(SDL_FINGERUP, 0, .3f, .7f);
    linux_touch_close(touch); empty();

    reset(1); active[0] = 12; xs[0] = 100; ys[0] = 800;
    touch = linux_touch_open("/dev/input/test", 90); CHECK(touch);
    finger(SDL_FINGERDOWN, 12, .2f, .1f);
    /* More than one read batch must be drained in a single frame. */
    for (i = 101; i <= 300; ++i) { emit(EV_ABS, ABS_MT_POSITION_X, i); emit(EV_SYN, SYN_REPORT, 0); }
    CHECK(linux_touch_pump(touch) == 200);
    for (i = 101; i <= 300; ++i) finger(SDL_FINGERMOTION, 12, .2f, i / 1000.f);
    linux_touch_close(touch); finger(SDL_FINGERUP, 12, .2f, .3f); empty();

    reset(2);
    touch = linux_touch_open("/dev/input/test", 0); CHECK(touch);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 20); emit(EV_ABS, ABS_MT_POSITION_X, 200);
    emit(EV_ABS, ABS_MT_POSITION_Y, 300); emit(EV_ABS, ABS_MT_SLOT, 1);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 21); emit(EV_ABS, ABS_MT_POSITION_X, 800);
    emit(EV_ABS, ABS_MT_POSITION_Y, 700); emit(EV_SYN, SYN_REPORT, 0);
    CHECK(linux_touch_pump(touch) == 2);
    finger(SDL_FINGERDOWN, 20, .2f, .3f); finger(SDL_FINGERDOWN, 21, .8f, .7f);
    emit(EV_ABS, ABS_MT_SLOT, 99); emit(EV_ABS, ABS_MT_TRACKING_ID, 77);
    emit(EV_SYN, SYN_REPORT, 0); CHECK(linux_touch_pump(touch) == 0); empty();
    active[0] = 22; xs[0] = 400; ys[0] = 500;
    emit(EV_SYN, SYN_DROPPED, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 99); /* Discard until SYN_REPORT. */
    emit(EV_SYN, SYN_REPORT, 0); CHECK(linux_touch_pump(touch) == 3);
    finger(SDL_FINGERUP, 20, .2f, .3f); finger(SDL_FINGERUP, 21, .8f, .7f);
    finger(SDL_FINGERDOWN, 22, .4f, .5f);
    linux_touch_stats(touch, &stats); CHECK(stats.overflows == 1 && stats.events == 5);
    disconnected = 1; CHECK(linux_touch_pump(touch) == -1);
    linux_touch_close(touch); finger(SDL_FINGERUP, 22, .4f, .5f); empty();
    disconnected = 0;
    touch = linux_touch_open("/dev/input/test", 0); CHECK(touch);
    finger(SDL_FINGERDOWN, 22, .4f, .5f);
    linux_touch_close(touch); finger(SDL_FINGERUP, 22, .4f, .5f); empty();
    reset(2); pointer_device = 1; CHECK(!linux_touch_open("/dev/input/test", 0)); empty();
    CHECK(!linux_touch_open("/dev/input/test", 45));
    SDL_Quit(); puts("PASS: single touch, MT, backlog, overflow and reconnect");
    return 0;
}
