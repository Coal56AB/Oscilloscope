#ifndef SIGNAL_FILTER_H
#define SIGNAL_FILTER_H
#include "signal_measure.h"
#include <stddef.h>
#include <stdint.h>
enum { SIGNAL_FILTER_DENOISE_RADIUS = 8, SIGNAL_FILTER_DENOISE_EDGE_POWER = 9 };

/* Centred moving average: OFF, 5, 17 or 65 acquired samples. No phase shift;
   source and result must be distinct. NaN gaps remain gaps, edges are truncated. */
void signal_filter_apply(const SignalNumber *source, size_t samples, unsigned level, SignalNumber *result);
/* Local variance Wiener estimate over 17 samples. Measured noise power sets
   smoothing; resolved edges retain their sample values. Distinct buffers. */
void signal_filter_denoise(const SignalNumber *source,size_t samples,SignalNumber noise_rms,SignalNumber *result);
/* Same estimate in ADC code units, with exact integer window statistics. */
void signal_filter_denoise_u8(const uint8_t *source,size_t samples,size_t stride,SignalNumber noise_rms,SignalNumber *result);
/* Required neighbours on either side when denoise precedes the manual filter. */
unsigned signal_filter_padding(unsigned level,int denoise);
/* Conservative output interval from bounds of raw values, window means and
   local variance. Uses the same Wiener rule as denoise; no signal is changed. */
void signal_filter_denoise_bounds(SignalNumber raw_low,SignalNumber raw_high,SignalNumber mean_low,SignalNumber mean_high,
    SignalNumber maximum_variance,SignalNumber noise_rms,SignalNumber *low,SignalNumber *high);
/* Filter bounded tiles with the complete centred support. Output is in
   physical units; source and result must not overlap. */
void signal_filter_trace(const SignalTrace *source,SignalNumber noise_rms,unsigned level,SignalNumber *result);
#endif
