#ifndef CAPTURE_ZYNQ_H
#define CAPTURE_ZYNQ_H
#include "capture.h"
#include "capture_uapi.h"
typedef struct {
    int fd, leased;
    void *mapping;
    struct osc_capture_info info;
    uint32_t index;
    double volts_per_code[2], zero_code[2];
} ZynqCaptureSource;
/* Requires a real driver implementing capture_uapi.h; errors are not simulated. */
int zynq_capture_open(ZynqCaptureSource *source, const char *device, const double volts_per_code[2],
                      const double zero_code[2]);
/* 1 leased block, 0 timeout, -1 error. The driver owns the mapping. */
int zynq_capture_acquire(ZynqCaptureSource *source, CaptureBuffer *buffer, unsigned timeout_ms);
int zynq_capture_release(ZynqCaptureSource *source);
void zynq_capture_close(ZynqCaptureSource *source);
#endif
