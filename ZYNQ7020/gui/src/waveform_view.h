#ifndef WAVEFORM_VIEW_H
#define WAVEFORM_VIEW_H
#include <stddef.h>
#include <stdint.h>

/* Pixel-space gap outside the recorded interval; never a voltage/sample value. */
#define WAVEFORM_NO_SAMPLE INT16_MIN
#define WAVEFORM_XY_POINTS 8192

typedef struct {
    const void *context;
    size_t samples;
    /* Bounds of original samples in [first, end), in source units.
       Return NaN bounds if that interval contains no valid sample. */
    void (*range)(const void *context, size_t first, size_t end, unsigned channel,
                  double *minimum, double *maximum);
    /* Optional representative value, distinct from a peak-detect envelope. */
    double (*value)(const void *context, size_t sample, unsigned channel);
    /* Optional bounds of envelope buckets [first, end). Each bucket spans
       envelope_samples source samples; extrema times and order are unknown. */
    void (*envelope)(const void *context, size_t first, size_t end, unsigned channel,
                     double *minimum, double *maximum);
    size_t envelope_samples;
} WaveformSource;

/* Select actual sample times uniformly in the recorded overlap. Calling this
   for both channels with the same interval preserves paired samples; envelope
   extrema never supply XY coordinates. Missing values retain their slot. */
size_t waveform_xy_project(const WaveformSource *source, double first, double samples,
                           unsigned channel, double zero, double scale,
                           size_t capacity, int16_t *points);

/* Preserve the original sample line and bounds; never interpolate peak times. */
void waveform_view_reduce(const WaveformSource *source, double first, double samples,
                          unsigned channel, double zero_y, double pixels_per_unit,
                          size_t columns, int16_t *representative, int16_t *minimum, int16_t *maximum);
/* The same segment rasterizer for filtered samples, retaining fractional codes. */
int waveform_view_connect_f64(const double *values, size_t count, double zero_y, double scale,
                              double first, double samples, size_t columns, size_t rows,
                              uint8_t *coverage);
/* Hidden excursions in native peak intervals, separate from acquired samples.
   Strength rises to 255 over four source quanta; every excursion is retained. */
void waveform_view_peak_bounds(const WaveformSource *source, double first, double samples,
                               unsigned channel, double zero_y, double pixels_per_unit,
                               double quantum, size_t columns, int16_t *minimum, int16_t *maximum,
                               uint8_t *strength);
/* Rasterize consecutive byte samples, not a decimated representative curve.
   Coverage is display-only; it never contributes to sample hit counts. */
int waveform_view_connect_u8(const uint8_t *data, size_t count, size_t stride,
                              const double positions[256], double first, double samples,
                              size_t columns, size_t rows, uint8_t *coverage);
/* Count original samples, without interpolated hits, envelope filling or blur. */
void waveform_density_column(const WaveformSource *source, double first, double samples,
                             unsigned channel, double zero_y, double pixels_per_unit,
                             size_t columns, size_t column, size_t rows, uint16_t *counts);
unsigned waveform_density_level(unsigned count, unsigned peak);
#endif
