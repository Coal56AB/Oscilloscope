#include "capture.h"
#include <string.h>
int capture_buffer_valid(const CaptureBuffer *b)
{
    return b && b->data && b->bytes && b->bytes <= b->capacity && !(b->bytes % 2) &&
           b->sample_rate_hz && b->first_sample <= UINT64_MAX - b->bytes / 2 &&
           (b->trigger_sample == CAPTURE_NO_TRIGGER ||
            (b->trigger_sample >= b->first_sample &&
             b->trigger_sample < b->first_sample + b->bytes / 2));
}
int capture_ring_init(CaptureRing *r, uint8_t *const *storage, size_t count, size_t bytes,
                      CapturePolicy policy)
{
    size_t i;
    if (!r || !storage || !count || count > CAPTURE_RING_MAX || !bytes || bytes % 2 ||
        policy < CAPTURE_STOP_ON_FULL || policy > CAPTURE_OVERWRITE_READY)
        return 0;
    for (i = 0; i < count; ++i)
        if (!storage[i])
            return 0;
    memset(r, 0, sizeof(*r));
    scope_mutex_init(&r->mutex);
    scope_condition_init(&r->changed);
    r->count = count;
    r->policy = policy;
    for (i = 0; i < count; ++i) {
        r->buffer[i].data = storage[i];
        r->buffer[i].capacity = bytes;
    }
    return 1;
}
void capture_ring_destroy(CaptureRing *r)
{
    scope_condition_destroy(&r->changed);
    scope_mutex_destroy(&r->mutex);
}
int capture_ring_begin(CaptureRing *r)
{
    size_t i;
    int slot = -1;
    uint64_t oldest = UINT64_MAX;
    scope_mutex_lock(&r->mutex);
    if (!r->stopped) {
        for (i = 0; i < r->count; ++i)
            if (r->state[i] == CAPTURE_FREE) {
                slot = (int)i;
                break;
            }
        if (slot < 0 && r->policy == CAPTURE_OVERWRITE_READY)
            for (i = 0; i < r->count; ++i)
                if (r->state[i] == CAPTURE_READY && r->buffer[i].sequence <= oldest) {
                    oldest = r->buffer[i].sequence;
                    slot = (int)i;
                }
        if (slot >= 0) {
            if (r->state[slot] == CAPTURE_READY) {
                ++r->overwritten;
                --r->occupancy;
            }
            r->state[slot] = CAPTURE_FILLING;
            r->buffer[slot].bytes = 0;
        } else {
            ++r->dropped;
            if (r->policy == CAPTURE_STOP_ON_FULL)
                r->stopped = 1;
        }
    }
    scope_mutex_unlock(&r->mutex);
    return slot;
}
int capture_ring_publish(CaptureRing *r, int slot)
{
    int ok = 0;
    scope_mutex_lock(&r->mutex);
    if (slot >= 0 && (size_t)slot < r->count && r->state[slot] == CAPTURE_FILLING &&
        capture_buffer_valid(&r->buffer[slot])) {
        r->state[slot] = CAPTURE_READY;
        ++r->published;
        ++r->occupancy;
        if (r->occupancy > r->peak_occupancy)
            r->peak_occupancy = r->occupancy;
        scope_condition_signal(&r->changed);
        ok = 1;
    }
    scope_mutex_unlock(&r->mutex);
    return ok;
}
void capture_ring_cancel(CaptureRing *r, int slot)
{
    scope_mutex_lock(&r->mutex);
    if (slot >= 0 && (size_t)slot < r->count && r->state[slot] == CAPTURE_FILLING)
        r->state[slot] = CAPTURE_FREE;
    scope_mutex_unlock(&r->mutex);
}
int capture_ring_take(CaptureRing *r, unsigned ms)
{
    size_t i;
    int slot = -1;
    uint64_t oldest = UINT64_MAX;
    scope_mutex_lock(&r->mutex);
    if (!r->occupancy && !r->stopped && ms)
        scope_condition_wait(&r->changed, &r->mutex, ms);
    for (i = 0; i < r->count; ++i)
        if (r->state[i] == CAPTURE_READY && r->buffer[i].sequence <= oldest) {
            oldest = r->buffer[i].sequence;
            slot = (int)i;
        }
    if (slot >= 0) {
        r->state[slot] = CAPTURE_READING;
        --r->occupancy;
    }
    scope_mutex_unlock(&r->mutex);
    return slot;
}
void capture_ring_release(CaptureRing *r, int slot)
{
    scope_mutex_lock(&r->mutex);
    if (slot >= 0 && (size_t)slot < r->count && r->state[slot] == CAPTURE_READING)
        r->state[slot] = CAPTURE_FREE;
    scope_mutex_unlock(&r->mutex);
}
void capture_ring_stop(CaptureRing *r)
{
    scope_mutex_lock(&r->mutex);
    r->stopped = 1;
    scope_condition_signal(&r->changed);
    scope_mutex_unlock(&r->mutex);
}
static int demo_read(CaptureSource *source, CaptureBuffer *b)
{
    DemoCaptureSource *s = source->context;
    size_t i;
    b->bytes = b->capacity & ~(size_t)1;
    b->sample_rate_hz = s->sample_rate_hz;
    b->first_sample = s->next_sample;
    b->sequence = s->sequence++;
    b->trigger_sample = CAPTURE_NO_TRIGGER;
    b->hardware_ns = 0;
    b->software_ns = scope_clock_ns();
    b->flags = 0;
    b->volts_per_code[0] = b->volts_per_code[1] = 0.01;
    b->zero_code[0] = b->zero_code[1] = 128;
    for (i = 0; i < b->bytes / 2; ++i) {
        unsigned phase = (unsigned)((s->next_sample + i) % s->period);
        b->data[2 * i] = (uint8_t)(phase < s->period / 2 ? 208 : 48);
        b->data[2 * i + 1] = (uint8_t)(64 + phase * 128 / s->period);
    }
    s->next_sample += b->bytes / 2;
    return b->bytes ? 1 : -1;
}
void demo_capture_source_init(CaptureSource *source, DemoCaptureSource *context, uint64_t rate)
{
    memset(context, 0, sizeof(*context));
    context->sample_rate_hz = rate;
    context->period = 256;
    source->context = context;
    source->read = demo_read;
    source->close = NULL;
}
