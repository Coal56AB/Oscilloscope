#include "waveform_view.h"
#include <limits.h>
#include <math.h>

static int16_t pixel_y(double value, double zero, double scale)
{
    double y = zero - value * scale;
    return (int16_t)(y < INT16_MIN ? INT16_MIN : y > INT16_MAX ? INT16_MAX : y);
}

void waveform_view_reduce(const WaveformSource *source, double first, double samples,
                          unsigned channel, double zero_y, double pixels_per_unit,
                          size_t columns, int16_t *representative, int16_t *minimum, int16_t *maximum)
{
    size_t x;
    double step;
    if (!source || !source->samples || !source->range || !columns ||
        !representative || !minimum || !maximum || !isfinite(first) || !isfinite(samples) ||
        !isfinite(zero_y) || !isfinite(pixels_per_unit) || samples <= 0.0)
        return;
    first = fmax(0.0, fmin(first, source->samples - 1.0));
    samples = fmin(samples, source->samples - first);
    step = samples / columns;
    for (x = 0; x < columns; ++x) {
        double position = fmin(first + x * step, source->samples - 1.0);
        size_t a = (size_t)floor(position);
        double low, high, value;
        if (step < 1.0) {
            double next_low, next_high, fraction = position - a;
            size_t next = a + 1 < source->samples ? a + 1 : a;
            source->range(source->context, a, a + 1, channel, &low, &high);
            source->range(source->context, next, next + 1, channel, &next_low, &next_high);
            low += (next_low - low) * fraction;
            high = low;
            value = low;
        } else {
            size_t end = (size_t)floor(first + (x + 1) * step);
            if (end <= a) end = a + 1;
            if (end > source->samples) end = source->samples;
            source->range(source->context, a, end, channel, &low, &high);
            {
                double unused;
                source->range(source->context, a, a + 1, channel, &value, &unused);
            }
        }
        minimum[x] = pixel_y(pixels_per_unit >= 0 ? high : low, zero_y, pixels_per_unit);
        maximum[x] = pixel_y(pixels_per_unit >= 0 ? low : high, zero_y, pixels_per_unit);
        representative[x] = pixel_y(value, zero_y, pixels_per_unit);
    }
}
