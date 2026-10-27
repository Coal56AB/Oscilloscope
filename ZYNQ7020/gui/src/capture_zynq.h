#ifndef CAPTURE_ZYNQ_H
#define CAPTURE_ZYNQ_H
#include "capture.h"
#include "capture_uapi.h"
typedef struct {
    int fd, leased;
    void *mapping;
    struct osc_capture_info info;
    uint32_t index;
    uint64_t leases[2];
    uint8_t *decoded[CAPTURE_RING_MAX];
    double volts_per_code[2], zero_code[2];
    int probe_supported; /* 0 unknown, 1 supported, -1 older kernel. */
    int dsp_supported;
} ZynqCaptureSource;
typedef struct {
    CaptureBuffer metadata; /* No sample storage until decode_packet. */
    const uint8_t *payload;
    size_t payload_bytes;
    unsigned index,peak_detect;
    int bounds_valid;
    int window_bounds;
    int probe_valid;
    uint32_t probe_control,probe_sides;
    int dsp_valid,dsp_triggered;
    uint32_t dsp_control,dsp_arm_id;
    uint64_t dsp_sample;
    uint64_t bounds_sequence;
    uint8_t minimum[2],maximum[2]; /* Previous payload's representative samples. */
    unsigned minimum_sum[2],maximum_sum[2],maximum_variance[2]; /* 17-point integer statistics. */
} ZynqCapturePacket;
/* Lease an encoded DMA packet without reading its payload. Optional bounds
   describe the preceding sequence; old FPGA headers leave bounds_valid=0. */
int zynq_capture_dequeue(ZynqCaptureSource *source,ZynqCapturePacket *packet,unsigned timeout_ms);
/* Decode a contiguous range while its packet is leased. Peak ranges are pair
   aligned. Prefer supplied storage; otherwise use the adapter's slot storage. */
int zynq_capture_decode_packet(ZynqCaptureSource *source,const ZynqCapturePacket *packet,
    size_t first,size_t samples,CaptureBuffer *buffer,uint8_t *data,uint8_t *peaks,size_t capacity);
/* Requires a real driver implementing capture_uapi.h; errors are not simulated. */
int zynq_capture_open(ZynqCaptureSource *source, const char *device, const double volts_per_code[2],
                      const double zero_code[2]);
/* 1 leased block, 0 timeout, -1 error. The driver owns the mapping. */
int zynq_capture_configure(ZynqCaptureSource *source, unsigned rate, unsigned shift);
/* Optional driver extension; ENOTTY keeps legacy capture available. */
int zynq_capture_set_probe(ZynqCaptureSource *source,uint32_t control);
/* Atomic re-arm is allowed with leases: sample storage is not reconfigured. */
int zynq_capture_set_dsp(ZynqCaptureSource *source,const struct osc_capture_dsp *configuration);
int zynq_capture_test_clock(ZynqCaptureSource *source, unsigned shift, unsigned payload_shift);
int zynq_capture_acquire(ZynqCaptureSource *source, CaptureBuffer *buffer, unsigned timeout_ms);
/* Prefer caller-owned storage, avoiding a second block copy. If the packet
   does not fit, use adapter storage as acquire does. capacity is bytes in
   each of data and peaks. Both paths retain the DEQUEUE/RELEASE lease. */
int zynq_capture_acquire_into(ZynqCaptureSource *source,CaptureBuffer *buffer,unsigned timeout_ms,
    uint8_t *data,uint8_t *peaks,size_t capacity);
int zynq_capture_ready(ZynqCaptureSource *source);
int zynq_capture_release(ZynqCaptureSource *source);
int zynq_capture_release_index(ZynqCaptureSource *source, uint32_t index);
void zynq_capture_close(ZynqCaptureSource *source);
#endif
