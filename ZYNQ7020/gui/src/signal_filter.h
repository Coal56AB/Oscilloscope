#ifndef SIGNAL_FILTER_H
#define SIGNAL_FILTER_H
#include <stddef.h>

/* Centred moving average: OFF, 5, 17 or 65 acquired samples. No phase shift;
   source and result must be distinct. NaN gaps remain gaps, edges are truncated. */
void signal_filter_apply(const double *source, size_t samples, unsigned level, double *result);
/* Local variance Wiener estimate over 17 samples. Measured noise power sets
   smoothing; resolved edges retain their sample values. Distinct buffers. */
void signal_filter_denoise(const double *source,size_t samples,double noise_rms,double *result);
#endif
