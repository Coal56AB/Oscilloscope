/* On-board DMA/lease check. Run with the GUI stopped. */
#define _POSIX_C_SOURCE 200809L
#include "capture_uapi.h"
#include "capture_settings.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static uint32_t hash_block(const volatile uint32_t *data, size_t bytes)
{
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < bytes / sizeof(*data); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static int run(unsigned pass, unsigned rate_index)
{
    struct osc_capture_info info = {0};
    struct osc_capture_block block = {0};
    struct pollfd p = {0};
    unsigned char *memory = MAP_FAILED;
    uint32_t held = UINT32_MAX, hash = 0;
    uint64_t blocks = 0, previous = 0, gaps = 0;
    unsigned rate = osc_capture_rate(rate_index);
    struct osc_capture_config config = {rate_index, capture_payload_shift(rate)};
    unsigned payload = 8u << config.payload_shift;
    double begin = 0, elapsed;
    int fd = open("/dev/oscill-capture", O_RDWR | O_NONBLOCK), result = 1;
    if (fd < 0) { perror("open capture"); return 1; }
    if (ioctl(fd, OSC_CAPTURE_GET_INFO, &info)) goto out;
    if (info.abi_version != OSC_CAPTURE_ABI || info.block_bytes != 2 * 1024 * 1024) goto out;
    if (ioctl(fd, OSC_CAPTURE_CONFIGURE, &config)) goto out;
    memory = mmap(NULL, info.mapping_bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) goto out;
    p.fd = fd; p.events = POLLIN;
    begin = seconds();
    while (seconds() - begin < 3) {
        int ready = poll(&p, 1, 200);
        if (ready < 0 || p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "capture poll error, revents=%x\n", p.revents);
            goto out;
        }
        if (!ready) continue;
        if (ioctl(fd, OSC_CAPTURE_DEQUEUE, &block)) {
            if (errno == EAGAIN) continue;
            goto out;
        }
        if (block.index >= info.buffers || block.data_offset != 64 ||
            block.bytes != payload || block.sample_rate_hz != rate ||
            block.first_sample != block.sequence * (payload / 2) ||
            (blocks && block.sequence <= previous)) goto out;
        if (blocks) gaps += block.sequence - previous - 1;
        previous = block.sequence;
        ++blocks;
        if (held == UINT32_MAX) {
            held = block.index;
            hash = hash_block((volatile uint32_t *)(memory + held * info.block_bytes), payload + 64);
            if (!ioctl(fd, OSC_CAPTURE_CONFIGURE, &config) || errno != EBUSY) goto out;
        } else if (ioctl(fd, OSC_CAPTURE_RELEASE, &block.index)) goto out;
    }
    elapsed = seconds() - begin;
    if (!blocks || held == UINT32_MAX ||
        hash != hash_block((volatile uint32_t *)(memory + held * info.block_bytes), payload + 64)) {
        fprintf(stderr, "No blocks or leased memory changed\n"); goto out;
    }
    printf("pass=%u rate=%u payload=%u blocks=%" PRIu64 " gaps=%" PRIu64 " seconds=%.3f delivered_MBps=%.2f lease=stable\n",
           pass, rate, payload, blocks, gaps, elapsed, blocks * (double)payload / elapsed / 1e6);
    result = 0;
out:
    if (result) perror("capture check");
    if (held != UINT32_MAX) ioctl(fd, OSC_CAPTURE_RELEASE, &held);
    if (memory != MAP_FAILED) munmap(memory, info.mapping_bytes);
    begin = seconds();
    close(fd);
    printf("pass=%u close_ms=%.2f\n", pass, (seconds() - begin) * 1000);
    return result;
}

int main(int argc, char **argv)
{
    unsigned pass, rate = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    if (!osc_capture_rate(rate)) return 2;
    for (pass = 1; pass <= 2; ++pass)
        if (run(pass, rate)) return 1;
    return 0;
}
