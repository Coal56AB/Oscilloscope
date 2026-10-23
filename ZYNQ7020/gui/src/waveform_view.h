#ifndef WAVEFORM_VIEW_H
#define WAVEFORM_VIEW_H
#include <stddef.h>
#include <stdint.h>

/* Pixel-space gap outside the recorded interval; never a voltage/sample value. */
#define WAVEFORM_NO_SAMPLE INT16_MIN

typedef struct {
    const void *context;
    size_t samples;
    /* Bounds of original samples in [first, end), in source units.
       Return NaN bounds if that interval contains no valid sample. */
    void (*range)(const void *context, size_t first, size_t end, unsigned channel,
                  double *minimum, double *maximum);
} WaveformSource;

/* Preserve the requested time interval, marking columns outside the record as gaps.
   Interpolate only when there are fewer samples than columns. */
void waveform_view_reduce(const WaveformSource *source, double first, double samples,
                          unsigned channel, double zero_y, double pixels_per_unit,
                          size_t columns, int16_t *representative, int16_t *minimum, int16_t *maximum);
#endif
