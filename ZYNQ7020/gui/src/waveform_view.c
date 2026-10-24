#include "waveform_view.h"
#include <limits.h>
#include <math.h>
#include <string.h>

unsigned waveform_density_level(unsigned count, unsigned peak)
{
    if (!count || !peak) return 0;
    return peak <= 1 ? 255 : 16 + (count - 1) * 239 / (peak - 1);
}

void waveform_density_column(const WaveformSource *source, double first, double samples,
                             unsigned channel, double zero_y, double pixels_per_unit,
                             size_t columns, size_t column, size_t rows, uint16_t *counts)
{
    size_t i, end;
    double left, right;
    memset(counts, 0, rows * sizeof(*counts));
    if (!source || !source->range || !columns || samples <= 0) return;
    left = first + column * samples / columns;
    right = first + (column + 1) * samples / columns;
    i = (size_t)ceil(fmax(0, fmin(left, source->samples)));
    end = (size_t)ceil(fmax(0, fmin(right, source->samples)));
    for (; i < end; ++i) {
        double value, unused, position;
        size_t y;
        if(source->value)value=source->value(source->context,i,channel);
        else source->range(source->context, i, i + 1, channel, &value, &unused);
        position = zero_y - value * pixels_per_unit;
        if (!isfinite(position) || position < 0 || position >= rows) continue;
        y = (size_t)position;
        if (counts[y] < UINT16_MAX) ++counts[y];
    }
}

static int16_t pixel_y(double value, double zero, double scale)
{
    double y = zero - value * scale;
    return (int16_t)(y <= INT16_MIN ? INT16_MIN + 1 : y > INT16_MAX ? INT16_MAX : y);
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
    step = samples / columns;
    for (x = 0; x < columns; ++x) {
        double left = first + x * step, right = first + (x + 1) * step;
        double position;
        size_t a;
        double low, high, value;
        representative[x] = minimum[x] = maximum[x] = WAVEFORM_NO_SAMPLE;
        if (right <= 0.0 || left >= source->samples) continue;
        position = fmax(0.0, fmin(left, source->samples - 1.0));
        a = (size_t)floor(position);
        if (step < 1.0) {
            /* Connect representative samples without stretching their peak
               buckets into the unknown interval between sample positions. */
            size_t next = a + 1 < source->samples ? a + 1 : a;
            double next_value, fraction = position - a;
            if (source->value) {
                value = source->value(source->context, a, channel);
                next_value = source->value(source->context, next, channel);
            } else {
                source->range(source->context, a, a + 1, channel, &value, &high);
                source->range(source->context, next, next + 1, channel, &next_value, &high);
            }
            if (!isfinite(next_value)) next_value = value;
            value += (next_value - value) * fraction;
            low = high = value;
        } else {
            size_t end = (size_t)floor(fmin(right, source->samples));
            if (end <= a) end = a + 1;
            if (end > source->samples) end = source->samples;
            source->range(source->context, a, end, channel, &low, &high);
            {
                double unused;
                if(source->value)value=source->value(source->context,a,channel);
                else source->range(source->context, a, a + 1, channel, &value, &unused);
                if (!isfinite(value)) value = (low + high) * 0.5;
            }
        }
        if (source->envelope && source->envelope_samples) {
            double span = (double)source->envelope_samples;
            double buckets = ceil(source->samples / span);
            size_t begin = (size_t)fmax(0.0, fmin(buckets, ceil(left / span - 0.5)));
            size_t end = (size_t)fmax(0.0, fmin(buckets, ceil(right / span - 0.5)));
            if (begin < end) {
                double envelope_low, envelope_high;
                source->envelope(source->context, begin, end, channel,
                                 &envelope_low, &envelope_high);
                if (isfinite(envelope_low) && envelope_low < low) low = envelope_low;
                if (isfinite(envelope_high) && envelope_high > high) high = envelope_high;
            }
        }
        if (!isfinite(low) || !isfinite(high) || !isfinite(value)) continue;
        minimum[x] = pixel_y(pixels_per_unit >= 0 ? high : low, zero_y, pixels_per_unit);
        maximum[x] = pixel_y(pixels_per_unit >= 0 ? low : high, zero_y, pixels_per_unit);
        representative[x] = pixel_y(value, zero_y, pixels_per_unit);
    }
}
