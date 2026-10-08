#include "capture_pipeline.h"
#include "capture_file.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct CapturePipeline {
    CaptureRing ring;
    ScopeMutex mutex;
    ScopeThread acquisition, processing;
    CaptureSource source;
    DemoCaptureSource demo;
    FileCaptureSource file_source;
    FILE *file;
    uint8_t *storage[CAPTURE_RING_MAX];
    CaptureRequest request;
    DisplayFrame frame;
    unsigned revision;
    int stop, have_frame, source_error;
    uint64_t bytes, display_replaced, processing_ns;
};
static int stopped(CapturePipeline *p)
{
    int result;
    scope_mutex_lock(&p->mutex);
    result = p->stop;
    scope_mutex_unlock(&p->mutex);
    return result;
}
static void acquisition_worker(void *context)
{
    CapturePipeline *p = context;
    while (!stopped(p)) {
        int slot, run, result;
        scope_mutex_lock(&p->mutex);
        run = p->request.running;
        scope_mutex_unlock(&p->mutex);
        if (!run) {
            scope_sleep_ms(10);
            continue;
        }
        slot = capture_ring_begin(&p->ring);
        if (slot < 0) {
            scope_sleep_ms(5);
            continue;
        }
        result = p->source.read(&p->source, &p->ring.buffer[slot]);
        if (result != 1) {
            capture_ring_cancel(&p->ring, slot);
            scope_mutex_lock(&p->mutex);
            if (result < 0)
                p->source_error = 1;
            p->request.running = 0;
            scope_mutex_unlock(&p->mutex);
            continue;
        }
        scope_mutex_lock(&p->mutex);
        p->bytes += p->ring.buffer[slot].bytes;
        scope_mutex_unlock(&p->mutex);
        if (!capture_ring_publish(&p->ring, slot))
            capture_ring_cancel(&p->ring, slot);
        /* Demo cadence only; hardware waits for DMA completion in its adapter. */
        scope_sleep_ms(20);
    }
}
static void processing_worker(void *context)
{
    CapturePipeline *p = context;
    int retained = -1;
    unsigned processed_revision = UINT32_MAX;
    while (!stopped(p)) {
        int slot = capture_ring_take(&p->ring, 20);
        CaptureRequest request;
        unsigned revision;
        if (slot >= 0) {
            if (retained >= 0)
                capture_ring_release(&p->ring, retained);
            retained = slot;
        }
        scope_mutex_lock(&p->mutex);
        request = p->request;
        revision = p->revision;
        scope_mutex_unlock(&p->mutex);
        if (retained >= 0 && (slot >= 0 || revision != processed_revision)) {
            CaptureBuffer *b = &p->ring.buffer[retained];
            CaptureView view;
            DisplayFrame frame;
            size_t total = b->bytes / 2;
            double samples = request.duration_seconds * b->sample_rate_hz;
            view.samples = samples >= total ? total : samples < 1 ? 1 : (size_t)samples;
            view.start = (size_t)((total - view.samples) * request.pan_fraction);
            memcpy(view.zero_y, request.zero_y, sizeof(view.zero_y));
            memcpy(view.pixels_per_volt, request.pixels_per_volt, sizeof(view.pixels_per_volt));
            if (capture_process(b, &view, &frame)) {
                scope_mutex_lock(&p->mutex);
                if (p->have_frame)
                    ++p->display_replaced;
                p->frame = frame;
                p->have_frame = 1;
                p->processing_ns = frame.processing_ns;
                scope_mutex_unlock(&p->mutex);
            }
            processed_revision = revision;
        }
    }
    if (retained >= 0)
        capture_ring_release(&p->ring, retained);
}
CapturePipeline *capture_pipeline_create(const char *path)
{
    CapturePipeline *p = calloc(1, sizeof(*p));
    size_t i, count = 8, bytes = 2 * 1024 * 1024;
    if (!p)
        return NULL;
    if (path) {
        p->file = fopen(path, "rb");
        if (!p->file || !file_capture_source_init(&p->source, &p->file_source, p->file))
            goto fail;
        bytes = p->file_source.metadata.bytes;
        count = 1;
        if (bytes > 256 * 1024 * 1024)
            goto fail;
    } else
        demo_capture_source_init(&p->source, &p->demo, 200000000);
    for (i = 0; i < count; ++i) {
        p->storage[i] = malloc(bytes);
        if (!p->storage[i])
            goto fail;
    }
    scope_mutex_init(&p->mutex);
    capture_ring_init(&p->ring, p->storage, count, bytes, CAPTURE_OVERWRITE_READY);
    p->request.duration_seconds = 0.001;
    p->request.pan_fraction = 0.5;
    p->request.running = 1;
    p->request.zero_y[0] = 200;
    p->request.zero_y[1] = 330;
    p->request.pixels_per_volt[0] = 120;
    p->request.pixels_per_volt[1] = 60;
    if (!scope_thread_start(&p->acquisition, acquisition_worker, p))
        goto fail_ring;
    if (!scope_thread_start(&p->processing, processing_worker, p)) {
        scope_mutex_lock(&p->mutex);
        p->stop = 1;
        scope_mutex_unlock(&p->mutex);
        scope_thread_join(&p->acquisition);
        goto fail_ring;
    }
    return p;
fail_ring:
    capture_ring_destroy(&p->ring);
    scope_mutex_destroy(&p->mutex);
fail:
    for (i = 0; i < CAPTURE_RING_MAX; ++i)
        free(p->storage[i]);
    if (p->file)
        fclose(p->file);
    free(p);
    return NULL;
}
void capture_pipeline_destroy(CapturePipeline *p)
{
    size_t i;
    if (!p)
        return;
    scope_mutex_lock(&p->mutex);
    p->stop = 1;
    scope_mutex_unlock(&p->mutex);
    capture_ring_stop(&p->ring);
    scope_thread_join(&p->acquisition);
    scope_thread_join(&p->processing);
    capture_ring_destroy(&p->ring);
    scope_mutex_destroy(&p->mutex);
    for (i = 0; i < CAPTURE_RING_MAX; ++i)
        free(p->storage[i]);
    if (p->file)
        fclose(p->file);
    free(p);
}
void capture_pipeline_request(CapturePipeline *p, const CaptureRequest *r)
{
    unsigned ch;
    if (!p || !r || !isfinite(r->duration_seconds) || r->duration_seconds <= 0 ||
        !isfinite(r->pan_fraction) || r->pan_fraction < 0 || r->pan_fraction > 1)
        return;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(r->zero_y[ch]) || !isfinite(r->pixels_per_volt[ch]))
            return;
    scope_mutex_lock(&p->mutex);
    if (p->request.duration_seconds != r->duration_seconds ||
        p->request.pan_fraction != r->pan_fraction ||
        memcmp(p->request.zero_y, r->zero_y, sizeof(r->zero_y)) ||
        memcmp(p->request.pixels_per_volt, r->pixels_per_volt, sizeof(r->pixels_per_volt)))
        ++p->revision;
    p->request = *r;
    scope_mutex_unlock(&p->mutex);
}
int capture_pipeline_frame(CapturePipeline *p, DisplayFrame *f)
{
    int result;
    scope_mutex_lock(&p->mutex);
    result = p->have_frame;
    if (result) {
        *f = p->frame;
        p->have_frame = 0;
    }
    scope_mutex_unlock(&p->mutex);
    return result;
}
void capture_pipeline_metrics(CapturePipeline *p, CaptureMetrics *m)
{
    memset(m, 0, sizeof(*m));
    scope_mutex_lock(&p->ring.mutex);
    m->blocks = p->ring.published;
    m->dropped = p->ring.dropped;
    m->overwritten = p->ring.overwritten;
    m->occupancy = p->ring.occupancy;
    m->peak_occupancy = p->ring.peak_occupancy;
    scope_mutex_unlock(&p->ring.mutex);
    scope_mutex_lock(&p->mutex);
    m->bytes = p->bytes;
    m->display_replaced = p->display_replaced;
    m->processing_ns = p->processing_ns;
    m->source_error = p->source_error;
    scope_mutex_unlock(&p->mutex);
}
