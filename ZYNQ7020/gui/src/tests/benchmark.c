#include "capture_processor.h"
#include "demo_signal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { BLOCK_BYTES = 1024 * 1024, BLOCKS = 8 };
typedef struct {
    CaptureRing ring;
    uint8_t *pattern;
    uint64_t end_ns, start_ns, bytes, processed, corrupt, processing_ns;
    double rate;
} Bench;
static void producer(void *context)
{
    Bench *b = context;
    uint64_t sequence = 0, next = b->start_ns;
    while (scope_clock_ns() < b->end_ns) {
        int slot;
        uint64_t now = scope_clock_ns();
        if (now < next) {
            scope_sleep_ms(1);
            continue;
        }
        next += (uint64_t)(BLOCK_BYTES * 1e9 / b->rate);
        slot = capture_ring_begin(&b->ring);
        if (slot < 0)
            continue;
        {
            CaptureBuffer *buffer = &b->ring.buffer[slot];
            memcpy(buffer->data, b->pattern, BLOCK_BYTES);
            buffer->bytes = BLOCK_BYTES;
            buffer->sample_rate_hz = 200000000;
            buffer->sequence = sequence++;
            buffer->first_sample = buffer->sequence * (BLOCK_BYTES / 2);
            buffer->trigger_sample = CAPTURE_NO_TRIGGER;
            buffer->software_ns = now;
            buffer->zero_code[0] = buffer->zero_code[1] = 128;
            buffer->volts_per_code[0] = buffer->volts_per_code[1] = 0.01;
            if (capture_ring_publish(&b->ring, slot))
                b->bytes += BLOCK_BYTES;
        }
    }
    capture_ring_stop(&b->ring);
}
static void consumer(void *context)
{
    Bench *b = context;
    CaptureView view = {0, BLOCK_BYTES / 2, {200, 330}, {100, 100}};
    DisplayFrame frame;
    for (;;) {
        int slot = capture_ring_take(&b->ring, 20);
        if (slot >= 0) {
            CaptureBuffer *buffer = &b->ring.buffer[slot];
            size_t i;
            for (i = 0; i < BLOCK_BYTES; i += 4096)
                if (buffer->data[i] != b->pattern[i])
                    ++b->corrupt;
            if (capture_process(buffer, &view, &frame)) {
                ++b->processed;
                b->processing_ns += frame.processing_ns;
            }
            capture_ring_release(&b->ring, slot);
        } else {
            int done;
            scope_mutex_lock(&b->ring.mutex);
            done = b->ring.stopped;
            scope_mutex_unlock(&b->ring.mutex);
            if (done)
                break;
        }
    }
}
int main(int argc, char **argv)
{
    Bench bench = {0};
    ScopeThread source, processing;
    uint8_t *storage[BLOCKS];
    uint32_t *pixels;
    DemoSignal *demo;
    size_t i;
    uint64_t frames = 0, render_ns = 0, max_render = 0, last_frame, max_gap = 0;
    double seconds = argc > 2 ? atof(argv[2]) : 3, target = argc > 1 ? atof(argv[1]) : 400, elapsed;
    uint64_t cpu_start;
    if (seconds <= 0 || seconds > 3600 || target <= 0 || target > 2000)
        return 2;
    bench.rate = target * 1e6;
    bench.pattern = malloc(BLOCK_BYTES);
    pixels = malloc(SCOPE_WIDTH * SCOPE_HEIGHT * 4);
    demo = malloc(sizeof(*demo));
    if (!bench.pattern || !pixels || !demo)
        return 1;
    for (i = 0; i < BLOCK_BYTES; ++i)
        bench.pattern[i] = (uint8_t)i;
    for (i = 0; i < BLOCKS; ++i) {
        storage[i] = malloc(BLOCK_BYTES);
        if (!storage[i])
            return 1;
    }
    if (!capture_ring_init(&bench.ring, storage, BLOCKS, BLOCK_BYTES, CAPTURE_OVERWRITE_READY))
        return 1;
    demo_signal_init(demo);
    cpu_start = scope_process_cpu_ns();
    bench.start_ns = scope_clock_ns();
    bench.end_ns = bench.start_ns + (uint64_t)(seconds * 1e9);
    last_frame = bench.start_ns;
    if (!scope_thread_start(&source, producer, &bench))
        return 1;
    if (!scope_thread_start(&processing, consumer, &bench)) {
        capture_ring_stop(&bench.ring);
        scope_thread_join(&source);
        return 1;
    }
    while (scope_clock_ns() < bench.end_ns) {
        uint64_t begin = scope_clock_ns(), duration, gap = begin - last_frame;
        if (gap > max_gap)
            max_gap = gap;
        last_frame = begin;
        demo_signal_advance(demo);
        scope_screen_render(pixels, SCOPE_WIDTH, &demo->screen);
        duration = scope_clock_ns() - begin;
        render_ns += duration;
        if (duration > max_render)
            max_render = duration;
        ++frames;
        if (duration < 16666667)
            scope_sleep_ms((unsigned)((16666667 - duration) / 1000000));
    }
    scope_thread_join(&source);
    scope_thread_join(&processing);
    elapsed = (scope_clock_ns() - bench.start_ns) / 1e9;
    printf(
        "software_only target_MBps=%.0f seconds=%.3f published_MBps=%.2f processed_MBps=%.2f "
        "blocks=%llu processed=%llu overwritten=%llu dropped=%llu peak=%zu corrupt=%llu fps=%.2f "
        "render_mean_ms=%.3f render_max_ms=%.3f frame_gap_max_ms=%.3f processing_mean_ms=%.3f "
        "cpu_core_percent=%.1f ring_MiB=%u\n",
        target, elapsed, bench.bytes / seconds / 1e6, bench.processed * BLOCK_BYTES / elapsed / 1e6,
        (unsigned long long)bench.ring.published, (unsigned long long)bench.processed,
        (unsigned long long)bench.ring.overwritten, (unsigned long long)bench.ring.dropped,
        bench.ring.peak_occupancy, (unsigned long long)bench.corrupt, frames / seconds,
        frames ? render_ns / 1e6 / frames : 0, max_render / 1e6, max_gap / 1e6,
        bench.processed ? bench.processing_ns / 1e6 / bench.processed : 0,
        (scope_process_cpu_ns() - cpu_start) / 1e7 / elapsed, BLOCK_BYTES * BLOCKS / 1024 / 1024);
    capture_ring_destroy(&bench.ring);
    for (i = 0; i < BLOCKS; ++i)
        free(storage[i]);
    free(bench.pattern);
    free(pixels);
    free(demo);
    return bench.corrupt ? 1 : 0;
}
