#include "capture_history.h"
#include <string.h>
void capture_history_init(CaptureHistory *h, uint8_t *data, uint8_t *peaks, size_t samples)
{
    memset(h, 0, sizeof(*h)); h->data = data; h->peaks=peaks; h->capacity = samples;
}
void capture_history_append(CaptureHistory *h, const CaptureBuffer *b)
{
    size_t count = b->bytes / 2, offset = 0, first;
    if (!h->capacity || !capture_buffer_valid(b)) return;
    if(b->peaks && (!h->peaks || h->capacity%2))return;
    /* Never join samples across an acquisition gap or a rate change. */
    if (h->count && (b->first_sample != h->latest.first_sample + h->latest.bytes / 2 ||
        b->sample_rate_hz != h->latest.sample_rate_hz || !!b->peaks != !!h->latest.peaks)) h->count = h->write = 0;
    if (count > h->capacity) { offset = count - h->capacity; count = h->capacity; }
    first = h->capacity - h->write;
    if (first > count) first = count;
    memcpy(h->data + h->write * 2, b->data + offset * 2, first * 2);
    memcpy(h->data, b->data + (offset + first) * 2, (count - first) * 2);
    if(b->peaks){
        memcpy(h->peaks+h->write*2,b->peaks+offset*2,first*2);
        memcpy(h->peaks,b->peaks+(offset+first)*2,(count-first)*2);
    }
    h->write = (h->write + count) % h->capacity;
    h->count += count;
    if (h->count > h->capacity) h->count = h->capacity;
    h->latest = *b;
}
int capture_history_snapshot(const CaptureHistory *h, CaptureBuffer *b)
{
    uint8_t *data = b->data;
    uint8_t *peaks = b->peaks;
    size_t capacity = b->capacity, start, first;
    if (!h->count || capacity < h->count * 2) return 0;
    if(h->latest.peaks && !peaks)return 0;
    start = (h->write + h->capacity - h->count) % h->capacity;
    first = h->capacity - start;
    if (first > h->count) first = h->count;
    memcpy(data, h->data + start * 2, first * 2);
    memcpy(data + first * 2, h->data, (h->count - first) * 2);
    if(h->latest.peaks){
        memcpy(peaks,h->peaks+start*2,first*2);
        memcpy(peaks+first*2,h->peaks,(h->count-first)*2);
    }
    *b = h->latest;
    b->data = data; b->capacity = capacity; b->bytes = h->count * 2;
    b->peaks=h->latest.peaks?peaks:NULL;
    b->first_sample = h->latest.first_sample + h->latest.bytes / 2 - h->count;
    b->trigger_sample = CAPTURE_NO_TRIGGER;
    return 1;
}
