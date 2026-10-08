#define _POSIX_C_SOURCE 200809L
#include "capture_zynq.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
int zynq_capture_open(ZynqCaptureSource *s, const char *device, const double gain[2],
                      const double zero[2])
{
    unsigned ch;
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    s->mapping = MAP_FAILED;
    if (!gain || !zero)
        return 0;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(gain[ch]) || gain[ch] <= 0 || !isfinite(zero[ch]))
            return 0;
    s->fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0 || ioctl(s->fd, OSC_CAPTURE_GET_INFO, &s->info) < 0)
        goto fail;
    if (s->info.abi_version != OSC_CAPTURE_ABI || s->info.register_version != 1 ||
        s->info.sample_format != OSC_CAPTURE_SAMPLE_FORMAT || !s->info.buffers ||
        s->info.buffers > CAPTURE_RING_MAX || !s->info.block_bytes || s->info.block_bytes % 2 ||
        s->info.mapping_bytes != (uint64_t)s->info.buffers * s->info.block_bytes ||
        s->info.mapping_bytes > SIZE_MAX)
        goto fail;
    s->mapping = mmap(NULL, (size_t)s->info.mapping_bytes, PROT_READ, MAP_SHARED, s->fd, 0);
    if (s->mapping == MAP_FAILED)
        goto fail;
    memcpy(s->volts_per_code, gain, sizeof(s->volts_per_code));
    memcpy(s->zero_code, zero, sizeof(s->zero_code));
    return 1;
fail:
    zynq_capture_close(s);
    return 0;
}
int zynq_capture_acquire(ZynqCaptureSource *s, CaptureBuffer *b, unsigned timeout)
{
    struct osc_capture_block block;
    struct pollfd p = {s->fd, POLLIN, 0};
    int result;
    if (s->fd < 0 || s->leased || timeout > INT32_MAX)
        return -1;
    result = poll(&p, 1, (int)timeout);
    if (result < 0)
        return errno == EINTR ? 0 : -1;
    if (!result)
        return 0;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    memset(&block, 0, sizeof(block));
    if (ioctl(s->fd, OSC_CAPTURE_DEQUEUE, &block) < 0)
        return errno == EAGAIN ? 0 : -1;
    s->index = block.index;
    s->leased = 1;
    if (block.index >= s->info.buffers || !block.bytes || block.bytes > s->info.block_bytes ||
        block.bytes % 2 || block.reserved) {
        zynq_capture_release(s);
        return -1;
    }
    memset(b, 0, sizeof(*b));
    b->data = (uint8_t *)s->mapping + (size_t)block.index * s->info.block_bytes;
    b->capacity = s->info.block_bytes;
    b->bytes = block.bytes;
    b->sequence = block.sequence;
    b->sample_rate_hz = block.sample_rate_hz;
    b->first_sample = block.first_sample;
    b->trigger_sample = block.trigger_sample;
    b->hardware_ns = block.hardware_ns;
    b->software_ns = scope_clock_ns();
    b->flags = block.flags;
    memcpy(b->volts_per_code, s->volts_per_code, sizeof(b->volts_per_code));
    memcpy(b->zero_code, s->zero_code, sizeof(b->zero_code));
    if (!capture_buffer_valid(b)) {
        zynq_capture_release(s);
        return -1;
    }
    return 1;
}
int zynq_capture_release(ZynqCaptureSource *s)
{
    if (!s->leased)
        return 1;
    if (ioctl(s->fd, OSC_CAPTURE_RELEASE, &s->index) < 0)
        return 0;
    s->leased = 0;
    return 1;
}
void zynq_capture_close(ZynqCaptureSource *s)
{
    if (s->fd >= 0) {
        if (s->leased)
            zynq_capture_release(s);
        if (s->mapping != MAP_FAILED)
            munmap(s->mapping, (size_t)s->info.mapping_bytes);
        close(s->fd);
    }
    s->fd = -1;
    s->leased = 0;
    s->mapping = MAP_FAILED;
}
