#ifndef CAPTURE_PROCESSOR_H
#define CAPTURE_PROCESSOR_H
#include "capture.h"
#include "scope_screen.h"
typedef struct {
    size_t start, samples;
    double zero_y[2], pixels_per_volt[2];
    /* Optional screen interval in source samples, including space outside memory.
       Zero span keeps the strict start/samples API used by existing callers. */
    double display_first, display_samples;
    int skip_spectrum;
} CaptureView;
typedef struct {
    int16_t y[2][SCOPE_PLOT_WIDTH], y_min[2][SCOPE_PLOT_WIDTH], y_max[2][SCOPE_PLOT_WIDTH];
    int16_t overview[2][SCOPE_PLOT_WIDTH];
    uint8_t fft[2][SCOPE_FFT_BINS];
    double minimum[2], maximum[2], mean[2], rms[2], frequency_hz[2], duty[2];
    uint64_t sequence, sample_rate_hz, processing_ns;
    uint64_t trigger_sample;
    size_t view_start, view_samples;
    double trigger_x;
    int overview_start, overview_end;
} DisplayFrame;
/* Scans the recorded overlap, preserves spikes using per-column min/max;
   display_first/display_samples may extend beyond memory and produce gaps. */
int capture_process(const CaptureBuffer *buffer, const CaptureView *view, DisplayFrame *frame);
int capture_value_at(const CaptureBuffer *buffer, size_t sample, unsigned channel, double *volts);
int capture_find_trigger(const CaptureBuffer *buffer, unsigned channel, unsigned edge,
                         double volts, uint64_t first_allowed, uint64_t *sample);
#endif
