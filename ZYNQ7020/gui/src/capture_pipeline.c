#include "capture_pipeline.h"
#include "capture_file.h"
#include "capture_history.h"
#include "capture_settings.h"
#ifdef SCOPE_ENABLE_ZYNQ
#include "capture_zynq.h"
#endif
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
    unsigned revision, arm_revision;
    int stop, have_frame, source_error;
    uint64_t bytes, display_replaced, processing_ns, waveforms;
    int hardware;
#ifdef SCOPE_ENABLE_ZYNQ
    ZynqCaptureSource zynq;
    CaptureMetrics hardware_metrics;
#endif
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
static void process_frame(CapturePipeline *p, const CaptureBuffer *b,
                           const CaptureRequest *request, uint64_t preprocessing_ns)
{
    uint64_t started = scope_clock_ns();
    CaptureView view;
    DisplayFrame frame;
    double samples = request->duration_seconds * b->sample_rate_hz;
    double base_samples = fmax(request->overview_duration_seconds, request->duration_seconds) * b->sample_rate_hz;
    double base_first;
    memset(&view, 0, sizeof(view));
    view.display_samples = samples < 1 ? 1 : samples;
    base_first = ((double)(b->bytes / 2) - base_samples) * request->pan_fraction;
    if (b->trigger_sample != CAPTURE_NO_TRIGGER && b->trigger_sample >= b->first_sample)
        base_first = (double)(b->trigger_sample - b->first_sample) -
                     base_samples * (1.0 - request->pan_fraction);
    if (request->roll) base_first = (double)(b->bytes / 2) - base_samples +
        (request->pan_fraction - 0.5) * base_samples;
    view.display_first = base_first + (base_samples - view.display_samples) * 0.5 +
                         request->zoom_offset_seconds * b->sample_rate_hz;
    view.skip_spectrum = !request->spectrum_enabled;
    memcpy(view.zero_y, request->zero_y, sizeof(view.zero_y));
    memcpy(view.pixels_per_volt, request->pixels_per_volt, sizeof(view.pixels_per_volt));
    if (capture_process(b, &view, &frame)) {
        if (request->overview_duration_seconds > request->duration_seconds) {
            CaptureView base = view;
            DisplayFrame overview;
            base.display_samples = request->overview_duration_seconds * b->sample_rate_hz;
            base.display_first = base_first;
            base.skip_spectrum = 1;
            if (capture_process(b, &base, &overview))
                memcpy(frame.overview, overview.y, sizeof(frame.overview));
            frame.overview_start = (int)lround((view.display_first - base_first) * SCOPE_PLOT_WIDTH / base_samples);
            frame.overview_end = (int)lround((view.display_first + view.display_samples - base_first) * SCOPE_PLOT_WIDTH / base_samples);
        } else memcpy(frame.overview, frame.y, sizeof(frame.overview));
        if (b->trigger_sample == CAPTURE_NO_TRIGGER)
            frame.trigger_x = ((1.0 - request->pan_fraction) * base_samples -
                (base_samples - view.display_samples) * 0.5 - request->zoom_offset_seconds * b->sample_rate_hz) *
                SCOPE_PLOT_WIDTH / view.display_samples;
        frame.processing_ns = scope_clock_ns() - started + preprocessing_ns;
        scope_mutex_lock(&p->mutex);
        if (p->have_frame) ++p->display_replaced;
        p->frame = frame;
        ++p->waveforms;
        p->have_frame = 1;
        p->processing_ns = frame.processing_ns;
        scope_mutex_unlock(&p->mutex);
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
            process_frame(p, &p->ring.buffer[retained], &request, 0);
            processed_revision = revision;
        }
    }
    if (retained >= 0)
        capture_ring_release(&p->ring, retained);
}
#ifdef SCOPE_ENABLE_ZYNQ
static void hardware_worker(void *context)
{
    CapturePipeline *p = context;
    CaptureHistory history;
    CaptureBuffer debug = {0}, retained = {0}, candidate = {0};
    CaptureDebug previous_debug = {0};
    double previous_gain[2] = {0};
    uint64_t expected = 0, next_trigger = 0, forced = 0, last_display = 0;
    unsigned rate_index = UINT32_MAX, depth = 0, shift, processed_revision = UINT32_MAX;
    unsigned single_arm = UINT32_MAX;
    int have_retained = 0;
    debug.data = p->storage[3]; debug.capacity = CAPTURE_MAX_SAMPLES * 2;
    retained.data = p->storage[1]; retained.capacity = CAPTURE_MAX_SAMPLES * 2;
    candidate.data = p->storage[2]; candidate.capacity = CAPTURE_MAX_SAMPLES * 2;
    capture_history_init(&history, p->storage[0], 100000);
    while (!stopped(p)) {
        CaptureRequest request;
        unsigned revision, arm, wanted_depth, wanted_rate, ch;
        int failed, force, fresh = 0;
        uint64_t preprocessing = 0;
        scope_mutex_lock(&p->mutex);
        request = p->request; revision = p->revision; arm = p->arm_revision;
        failed = p->source_error;
        scope_mutex_unlock(&p->mutex);
        if (request.duration_seconds <= 0) { scope_sleep_ms(5); continue; }
        wanted_depth = request.record_samples ? request.record_samples : 100000;
        if (wanted_depth > CAPTURE_MAX_SAMPLES) wanted_depth = CAPTURE_MAX_SAMPLES;
        wanted_rate = capture_rate_index(fmax(request.duration_seconds, request.overview_duration_seconds),
                                         wanted_depth, request.roll);
        force = request.force_sequence != forced;
        if (memcmp(&previous_debug, &request.debug, sizeof(previous_debug)) ||
            memcmp(previous_gain, request.adc_volts_per_code, sizeof(previous_gain))) {
            capture_history_init(&history, p->storage[0], wanted_depth);
            last_display = 0; previous_debug = request.debug;
            memcpy(previous_gain, request.adc_volts_per_code, sizeof(previous_gain));
        }
        if ((request.running || force) && (wanted_rate != rate_index || wanted_depth != depth)) {
            shift = capture_payload_shift(osc_capture_rate(wanted_rate));
            if (!zynq_capture_configure(&p->zynq, wanted_rate, shift)) failed = 1;
            else {
                rate_index = wanted_rate; depth = wanted_depth;
                capture_history_init(&history, p->storage[0], depth);
                expected = next_trigger = last_display = 0; failed = 0;
            }
            scope_mutex_lock(&p->mutex); p->source_error = failed; scope_mutex_unlock(&p->mutex);
        }
        if (!failed && (force || (request.running && (request.trigger_mode != 2 || single_arm != arm)))) {
            CaptureBuffer received;
            int result = zynq_capture_acquire(&p->zynq, &received, 40);
            if (result < 0) {
                scope_mutex_lock(&p->mutex); p->source_error = 1; scope_mutex_unlock(&p->mutex);
            } else if (result > 0) {
                uint64_t started = scope_clock_ns();
                scope_mutex_lock(&p->mutex);
                ++p->hardware_metrics.blocks;
                if (received.sequence > expected) p->hardware_metrics.dropped += received.sequence - expected;
                p->bytes += received.bytes;
                scope_mutex_unlock(&p->mutex);
                expected = received.sequence + 1;
                for (ch = 0; ch < 2; ++ch)
                    if (isfinite(request.adc_volts_per_code[ch]) && request.adc_volts_per_code[ch] > 0)
                        received.volts_per_code[ch] = request.adc_volts_per_code[ch];
                if (received.bytes / 2 > depth) {
                    size_t skip = received.bytes / 2 - depth;
                    received.data += skip * 2; received.first_sample += skip;
                    received.bytes -= skip * 2; received.capacity -= skip * 2;
                }
                if (request.debug.enabled) {
                    if (!capture_debug_replace(&received, &debug, &request.debug)) failed = 1;
                    else capture_history_append(&history, &debug);
                } else capture_history_append(&history, &received);
                if (!zynq_capture_release(&p->zynq)) failed = 1;
                if (failed) {
                    scope_mutex_lock(&p->mutex); p->source_error = 1; scope_mutex_unlock(&p->mutex);
                    continue;
                }
                if (force || request.trigger_mode != 0 || revision != processed_revision || started - last_display >= 40000000) {
                    int triggered = 0;
                    capture_history_snapshot(&history, &candidate);
                    if (!request.roll) {
                        double span = fmax(request.duration_seconds, request.overview_duration_seconds) * candidate.sample_rate_hz;
                        double pre = ceil(span * (1.0 - request.pan_fraction));
                        double post = ceil(span * request.pan_fraction);
                        if (candidate.bytes / 2 >= pre + post) {
                            CaptureBuffer search = candidate;
                            uint64_t first = candidate.first_sample + (uint64_t)pre;
                            if (first < next_trigger) first = next_trigger;
                            search.bytes -= (size_t)post * 2;
                            triggered = capture_find_trigger(&search, request.trigger_channel, request.trigger_edge,
                                request.trigger_voltage, first, &candidate.trigger_sample);
                        }
                    }
                    if (force) {
                        candidate.trigger_sample = candidate.first_sample + candidate.bytes / 4;
                        triggered = 1; forced = request.force_sequence;
                    }
                    scope_mutex_lock(&p->mutex);
                    p->hardware_metrics.waiting = !triggered && request.trigger_mode != 0;
                    scope_mutex_unlock(&p->mutex);
                    if (triggered || request.trigger_mode == 0) {
                        CaptureBuffer swap = retained;
                        retained = candidate; candidate = swap;
                        have_retained = fresh = 1;
                        if (triggered) next_trigger = retained.trigger_sample +
                            request.holdoff_samples * retained.sample_rate_hz / 200000000 + 1;
                        if (request.trigger_mode == 2) single_arm = arm;
                        last_display = started;
                        preprocessing = scope_clock_ns() - started;
                    }
                }
            }
        } else scope_sleep_ms(5);
        if (have_retained && (fresh || revision != processed_revision)) {
            process_frame(p, &retained, &request, preprocessing);
            processed_revision = revision;
        }
    }
}
CapturePipeline *capture_pipeline_create_zynq(const char *device, const double gain[2],
                                               const double zero[2])
{
    CapturePipeline *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (!zynq_capture_open(&p->zynq, device, gain, zero)) { free(p); return NULL; }
    p->hardware = 1;
    {
        unsigned i;
        for (i = 0; i < 4; ++i) {
            p->storage[i] = malloc(CAPTURE_MAX_SAMPLES * 2);
            if (!p->storage[i]) goto fail;
        }
    }
    scope_mutex_init(&p->mutex);
    /* Start only after the frontend has supplied its first request. */
    if (!scope_thread_start(&p->processing, hardware_worker, p)) {
        scope_mutex_destroy(&p->mutex); goto fail;
    }
    return p;
fail:
    free(p->storage[0]); free(p->storage[1]); free(p->storage[2]); free(p->storage[3]); zynq_capture_close(&p->zynq); free(p); return NULL;
}
#endif
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
    if (!p->hardware) {
        capture_ring_stop(&p->ring);
        scope_thread_join(&p->acquisition);
    }
    scope_thread_join(&p->processing);
    if (!p->hardware) capture_ring_destroy(&p->ring);
#ifdef SCOPE_ENABLE_ZYNQ
    if (p->hardware) zynq_capture_close(&p->zynq);
#endif
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
    if (!isfinite(r->overview_duration_seconds) || r->overview_duration_seconds < 0 ||
        !isfinite(r->zoom_offset_seconds) || !isfinite(r->trigger_voltage) ||
        r->trigger_channel < 0 || r->trigger_channel > 1 ||
        r->trigger_edge < 0 || r->trigger_edge > 2 ||
        r->trigger_mode < 0 || r->trigger_mode > 2)
        return;
    scope_mutex_lock(&p->mutex);
    if (r->running && !p->request.running) ++p->arm_revision;
    if (p->request.duration_seconds != r->duration_seconds ||
        p->request.spectrum_enabled != r->spectrum_enabled ||
        p->request.roll != r->roll || p->request.record_samples != r->record_samples ||
        p->request.overview_duration_seconds != r->overview_duration_seconds ||
        p->request.zoom_offset_seconds != r->zoom_offset_seconds ||
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
    if (!p->hardware) {
    scope_mutex_lock(&p->ring.mutex);
    m->blocks = p->ring.published;
    m->dropped = p->ring.dropped;
    m->overwritten = p->ring.overwritten;
    m->occupancy = p->ring.occupancy;
    m->peak_occupancy = p->ring.peak_occupancy;
    scope_mutex_unlock(&p->ring.mutex);
    }
    scope_mutex_lock(&p->mutex);
#ifdef SCOPE_ENABLE_ZYNQ
    if (p->hardware) *m = p->hardware_metrics;
#endif
    m->bytes = p->bytes;
    m->display_replaced = p->display_replaced;
    m->processing_ns = p->processing_ns;
    m->waveforms = p->waveforms;
    m->source_error = p->source_error;
    scope_mutex_unlock(&p->mutex);
}
