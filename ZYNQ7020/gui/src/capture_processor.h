#ifndef CAPTURE_PROCESSOR_H
#define CAPTURE_PROCESSOR_H
#include "capture.h"
#include "scope_screen.h"
typedef struct {
    size_t start, samples;
    double zero_y[2], pixels_per_volt[2];
} CaptureView;
typedef struct {
    int16_t y[2][SCOPE_PLOT_WIDTH], y_min[2][SCOPE_PLOT_WIDTH], y_max[2][SCOPE_PLOT_WIDTH];
    uint8_t fft[2][SCOPE_FFT_BINS];
    double minimum[2], maximum[2], mean[2], rms[2], frequency_hz[2], duty[2];
    uint64_t sequence, sample_rate_hz, processing_ns;
    size_t view_start, view_samples;
} DisplayFrame;
/* Scans only the requested view, preserves spikes using per-column min/max. */
int capture_process(const CaptureBuffer *buffer, const CaptureView *view, DisplayFrame *frame);
int capture_value_at(const CaptureBuffer *buffer, size_t sample, unsigned channel, double *volts);
#endif
