#ifndef CAPTURE_DEBUG_H
#define CAPTURE_DEBUG_H
#include "capture.h"
#include "signal_generator.h"
typedef struct {
    int enabled, wave[2];
    unsigned frequency_hz;
    double offset_fraction[2];
    double amplitude[2]; /* Zero retains the original unit amplitude. */
} CaptureDebug;
/* Requires a completed, leased ADC block. Metadata and timing are preserved.
   Destination is separate software storage; DMA memory remains read-only. */
int capture_debug_replace(const CaptureBuffer *received, CaptureBuffer *simulated,
                           const CaptureDebug *debug);
#endif
