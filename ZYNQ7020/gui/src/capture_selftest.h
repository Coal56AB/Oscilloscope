#ifndef CAPTURE_SELFTEST_H
#define CAPTURE_SELFTEST_H
#include <stddef.h>
typedef void (*CaptureSelftestProgress)(unsigned clock_hz, unsigned step, void *context);
/* Real encode clocks, raw DMA and the four AD9288 branches; no GUI simulation. */
int capture_selftest(const char *device, CaptureSelftestProgress progress, void *context,
                     char *error, size_t error_size);
#endif
