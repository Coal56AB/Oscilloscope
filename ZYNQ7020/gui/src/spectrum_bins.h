#ifndef SPECTRUM_BINS_H
#define SPECTRUM_BINS_H

#include <math.h>

/* Common amplitude scale for the spectrum and its level cursor. */
static inline double spectrum_db_at_level(double level)
{
    return -80.0 + level * 80.0 / 255.0;
}

static inline double spectrum_level_at_db(double db)
{
    return (db + 80.0) * 255.0 / 80.0;
}

/* Logarithmic positive-frequency axis: first FFT frequency through Nyquist.
   DC is outside this axis. The first frequency equals Fs / FFT size. */
static inline double spectrum_frequency_at(double fraction, double span_hz, unsigned size)
{
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return span_hz * 2.0 / size * pow(size / 2.0, fraction);
}

/* Maximum over each logarithmic display group retains narrow spectral peaks.
   Below the FFT resolution, repeated nearest frequencies fill empty groups. */
static inline void spectrum_bin_range(unsigned index, unsigned count, unsigned size,
                                      unsigned *first, unsigned *end)
{
    double half = size / 2.0;
    *first = index ? (unsigned)ceil(pow(half, (index - 0.5) / (count - 1))) : 1;
    *end = index + 1 < count ?
        (unsigned)ceil(pow(half, (index + 0.5) / (count - 1))) : size / 2 + 1;
    if (*end <= *first) {
        *first = (unsigned)(pow(half, (double)index / (count - 1)) + 0.5);
        *end = *first + 1;
    }
}

#endif
