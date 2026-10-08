#ifndef CAPTURE_H
#define CAPTURE_H
#include "scope_thread.h"
#include <stddef.h>
#include <stdint.h>
#define CAPTURE_ABI_VERSION 1
#define CAPTURE_CHANNELS 2
#define CAPTURE_RING_MAX 128
#define CAPTURE_NO_TRIGGER UINT64_MAX
/* Interleaved unsigned offset binary: CH1[t], CH2[t], CH1[t+1], CH2[t+1]. */
typedef struct {
    uint8_t *data;
    size_t capacity, bytes;
    uint64_t sequence, sample_rate_hz, first_sample, trigger_sample, hardware_ns, software_ns;
    double volts_per_code[2], zero_code[2];
    uint32_t flags;
} CaptureBuffer;
typedef enum { CAPTURE_FREE, CAPTURE_FILLING, CAPTURE_READY, CAPTURE_READING } CaptureState;
typedef enum { CAPTURE_STOP_ON_FULL, CAPTURE_DROP_NEW, CAPTURE_OVERWRITE_READY } CapturePolicy;
typedef struct {
    ScopeMutex mutex;
    ScopeCondition changed;
    CaptureBuffer buffer[CAPTURE_RING_MAX];
    CaptureState state[CAPTURE_RING_MAX];
    size_t count, occupancy, peak_occupancy;
    CapturePolicy policy;
    uint64_t published, dropped, overwritten;
    int stopped;
} CaptureRing;
/* Storage is supplied by the owner; hardware uses driver-mapped DMA buffers. */
int capture_ring_init(CaptureRing *ring, uint8_t *const *storage, size_t count, size_t block_bytes,
                      CapturePolicy policy);
void capture_ring_destroy(CaptureRing *ring);
int capture_ring_begin(CaptureRing *ring);
int capture_ring_publish(CaptureRing *ring, int slot);
void capture_ring_cancel(CaptureRing *ring, int slot);
int capture_ring_take(CaptureRing *ring, unsigned timeout_ms);
void capture_ring_release(CaptureRing *ring, int slot);
void capture_ring_stop(CaptureRing *ring);
typedef struct CaptureSource CaptureSource;
struct CaptureSource {
    void *context;
    int (*read)(CaptureSource *source, CaptureBuffer *buffer); /* 1 block, 0 EOF, -1 error */
    void (*close)(CaptureSource *source);
};
typedef struct {
    uint64_t sample_rate_hz, next_sample, sequence;
    unsigned period;
} DemoCaptureSource;
void demo_capture_source_init(CaptureSource *source, DemoCaptureSource *context, uint64_t rate_hz);
int capture_buffer_valid(const CaptureBuffer *buffer);
#endif
