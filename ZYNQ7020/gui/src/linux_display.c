#define _POSIX_C_SOURCE 200809L
#include "linux_display.h"
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef SCOPE_ENABLE_DRM
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif
typedef struct {
    uint32_t handle, fb, pitch;
    uint64_t size;
    uint8_t *map;
} DumbBuffer;
struct LinuxDisplay {
    int fd, width, height, front, pending, framebuffer;
    uint32_t connector, crtc;
#ifdef SCOPE_ENABLE_DRM
    drmModeCrtc *previous;
#endif
    DumbBuffer buffer[2];
};
LinuxDisplay *linux_display_open_framebuffer(const char *device, int width, int height)
{
    LinuxDisplay *d = calloc(1, sizeof(*d));
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if (!d || width <= 0 || height <= 0) {
        free(d);
        return NULL;
    }
    d->fd = open(device, O_RDWR | O_CLOEXEC);
    d->framebuffer = 1;
    d->width = width;
    d->height = height;
    if (d->fd < 0 || ioctl(d->fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        ioctl(d->fd, FBIOGET_VSCREENINFO, &var) < 0 || fix.type != FB_TYPE_PACKED_PIXELS ||
        fix.visual != FB_VISUAL_TRUECOLOR || var.xres != (unsigned)width ||
        var.yres != (unsigned)height || var.xoffset || var.yoffset || var.bits_per_pixel != 32 ||
        var.transp.length ||
        var.red.offset != 16 || var.red.length != 8 || var.red.msb_right || var.green.offset != 8 ||
        var.green.length != 8 || var.green.msb_right || var.blue.offset != 0 ||
        var.blue.length != 8 || var.blue.msb_right || fix.line_length < (uint64_t)width * 4 ||
        (uint64_t)fix.line_length * height > fix.smem_len)
        goto fail;
    d->buffer[0].pitch = fix.line_length;
    d->buffer[0].size = fix.smem_len;
    d->buffer[0].map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, 0);
    if (d->buffer[0].map == MAP_FAILED) {
        d->buffer[0].map = NULL;
        goto fail;
    }
    return d;
fail:
    linux_display_close(d);
    return NULL;
}
#ifdef SCOPE_ENABLE_DRM
static int make_buffer(LinuxDisplay *d, DumbBuffer *b)
{
    struct drm_mode_create_dumb create = {0};
    struct drm_mode_map_dumb map = {0};
    create.width = d->width;
    create.height = d->height;
    create.bpp = 32;
    if (ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        return 0;
    b->handle = create.handle;
    b->pitch = create.pitch;
    b->size = create.size;
    if (drmModeAddFB(d->fd, d->width, d->height, 24, 32, b->pitch, b->handle, &b->fb))
        return 0;
    map.handle = b->handle;
    if (ioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
        return 0;
    b->map = mmap(NULL, (size_t)b->size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, map.offset);
    if (b->map == MAP_FAILED) {
        b->map = NULL;
        return 0;
    }
    memset(b->map, 0, (size_t)b->size);
    return 1;
}
LinuxDisplay *linux_display_open(const char *device, int width, int height)
{
    LinuxDisplay *d = calloc(1, sizeof(*d));
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;
    drmModeModeInfo mode = {0};
    int i, j;
    uint64_t capability = 0;
    if (!d)
        return NULL;
    d->fd = -1;
    d->pending = -1;
    d->width = width;
    d->height = height;
    d->fd = open(device, O_RDWR | O_CLOEXEC);
    if (d->fd < 0 || drmGetCap(d->fd, DRM_CAP_DUMB_BUFFER, &capability) || !capability)
        goto fail;
    if (drmSetMaster(d->fd))
        goto fail;
    resources = drmModeGetResources(d->fd);
    if (!resources)
        goto fail;
    for (i = 0; i < resources->count_connectors && !d->connector; ++i) {
        connector = drmModeGetConnector(d->fd, resources->connectors[i]);
        if (!connector)
            continue;
        if (connector->connection == DRM_MODE_CONNECTED)
            for (j = 0; j < connector->count_modes; ++j) {
                drmModeModeInfo candidate = connector->modes[j];
                if (candidate.hdisplay == width && candidate.vdisplay == height &&
                    candidate.vrefresh >= 59 && candidate.vrefresh <= 61) {
                    mode = candidate;
                    d->connector = connector->connector_id;
                    break;
                }
            }
        if (d->connector) {
            int e;
            for (e = 0; e < connector->count_encoders && !d->crtc; ++e) {
                drmModeEncoder *encoder = drmModeGetEncoder(d->fd, connector->encoders[e]);
                if (encoder) {
                    for (j = 0; j < resources->count_crtcs; ++j)
                        if (encoder->possible_crtcs & (1u << j)) {
                            d->crtc = resources->crtcs[j];
                            break;
                        }
                    drmModeFreeEncoder(encoder);
                }
            }
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }
    drmModeFreeResources(resources);
    resources = NULL;
    if (!d->connector || !d->crtc || !make_buffer(d, &d->buffer[0]) ||
        !make_buffer(d, &d->buffer[1]))
        goto fail;
    d->previous = drmModeGetCrtc(d->fd, d->crtc);
    if (drmModeSetCrtc(d->fd, d->crtc, d->buffer[0].fb, 0, 0, &d->connector, 1, &mode))
        goto fail;
    return d;
fail:
    if (connector)
        drmModeFreeConnector(connector);
    if (resources)
        drmModeFreeResources(resources);
    linux_display_close(d);
    return NULL;
}
static void flipped(int fd, unsigned frame, unsigned sec, unsigned usec, void *context)
{
    LinuxDisplay *d = context;
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    d->front = d->pending;
    d->pending = -1;
}
#else
LinuxDisplay *linux_display_open(const char *device, int width, int height)
{
    (void)device;
    (void)width;
    (void)height;
    return NULL;
}
#endif
int linux_display_present(LinuxDisplay *d, const uint32_t *pixels)
{
    int y;
    if (d->framebuffer) {
        for (y = 0; y < d->height; ++y)
            memcpy(d->buffer[0].map + (size_t)y * d->buffer[0].pitch, pixels + (size_t)y * d->width,
                   (size_t)d->width * 4);
        return 1;
    }
#ifdef SCOPE_ENABLE_DRM
    struct pollfd p = {d->fd, POLLIN, 0};
    int back, result;
    drmEventContext event = {0};
    event.version = DRM_EVENT_CONTEXT_VERSION;
    event.page_flip_handler = flipped;
    result = poll(&p, 1, 0);
    if (result < 0 || p.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    if (result > 0 && drmHandleEvent(d->fd, &event))
        return -1;
    if (d->pending >= 0)
        return 0;
    back = 1 - d->front;
    for (y = 0; y < d->height; ++y)
        memcpy(d->buffer[back].map + (size_t)y * d->buffer[back].pitch,
               pixels + (size_t)y * d->width, (size_t)d->width * 4);
    if (drmModePageFlip(d->fd, d->crtc, d->buffer[back].fb, DRM_MODE_PAGE_FLIP_EVENT, d))
        return -1;
    d->pending = back;
    return 1;
#else
    return -1;
#endif
}
void linux_display_close(LinuxDisplay *d)
{
    int i;
    if (!d)
        return;
#ifdef SCOPE_ENABLE_DRM
    if (d->previous && d->previous->mode_valid)
        drmModeSetCrtc(d->fd, d->previous->crtc_id, d->previous->buffer_id, d->previous->x,
                       d->previous->y, &d->connector, 1, &d->previous->mode);
    else if (d->crtc && d->fd >= 0)
        drmModeSetCrtc(d->fd, d->crtc, 0, 0, 0, NULL, 0, NULL);
    if (d->previous)
        drmModeFreeCrtc(d->previous);
#endif
    for (i = 0; i < 2; ++i) {
        DumbBuffer *b = &d->buffer[i];
        if (b->map)
            munmap(b->map, (size_t)b->size);
#ifdef SCOPE_ENABLE_DRM
        if (b->fb)
            drmModeRmFB(d->fd, b->fb);
        if (b->handle) {
            struct drm_mode_destroy_dumb destroy = {0};
            destroy.handle = b->handle;
            ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
#endif
    }
    if (d->fd >= 0)
        close(d->fd);
    free(d);
}
