#ifndef CAPTURE_PROCESSOR_H
#define CAPTURE_PROCESSOR_H
#include "capture.h"
#include "scope_screen.h"
#include "channel_response.h"
typedef struct {
    size_t start, samples;
    double zero_y[2], pixels_per_volt[2];
    /* Optional screen interval in source samples, including space outside memory.
       Zero span keeps the strict start/samples API used by existing callers. */
    double display_first, display_samples;
    int skip_spectrum;
    int density_enabled;
    unsigned density_rows;
    unsigned filter_level[2];
    double noise_rms[2];
    const ChannelResponse *response[2];
    const double *corrected[2];
    unsigned calibration_mask;
    unsigned baseline_mask;
} CaptureView;
typedef struct {
    int16_t y[2][SCOPE_PLOT_WIDTH], y_min[2][SCOPE_PLOT_WIDTH], y_max[2][SCOPE_PLOT_WIDTH];
    int16_t peak_min[2][SCOPE_PLOT_WIDTH], peak_max[2][SCOPE_PLOT_WIDTH];
    uint8_t peak_strength[2][SCOPE_PLOT_WIDTH];
    int16_t overview[2][SCOPE_PLOT_WIDTH];
    uint8_t fft[2][SCOPE_FFT_BINS];
    double minimum[2], maximum[2], mean[2], rms[2], frequency_hz[2], duty[2];
    double fundamental_hz[2],fundamental_amplitude[2];
    double volts_per_code[2];
    uint64_t sequence, sample_rate_hz, processing_ns;
    uint64_t capture_id;
    unsigned fft_size;
    uint64_t trigger_sample;
    size_t view_start, view_samples;
    double trigger_x, display_samples;
    int overview_start, overview_end;
    int density_valid;
    unsigned density_rows;
    int peak_detect;
    unsigned clipped_mask;
    unsigned calibration_mask;
    unsigned baseline_mask;
    uint8_t connections[2][SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    uint8_t density[2][SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
} DisplayFrame;
/* Scans the recorded overlap, preserves spikes using per-column min/max;
   display_first/display_samples may extend beyond memory and produce gaps. */
int capture_process(const CaptureBuffer *buffer, const CaptureView *view, DisplayFrame *frame);
int capture_value_at(const CaptureBuffer *buffer, size_t sample, unsigned channel, double *volts);
/* Qualify a crossing through both sides of a noise guard, retaining its original
   sample time. Nonzero hysteresis_codes is the minimum adaptive guard in codes.
   Zero preserves direct threshold crossing without noise rejection. */
int capture_find_trigger(const CaptureBuffer *buffer, unsigned channel, unsigned edge,
                         double volts, unsigned hysteresis_codes,
                         uint64_t first_allowed, uint64_t *sample);
/* Trigger on the same centered sample filter as the displayed channel. */
int capture_find_trigger_filtered(const CaptureBuffer *buffer,unsigned channel,unsigned edge,
                                  double volts,unsigned hysteresis_codes,unsigned filter_level,double noise_rms,
                                  uint64_t first_allowed,uint64_t *sample);
#endif
