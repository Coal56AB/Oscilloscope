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

static double sample_value(const WaveformSource *source, size_t sample, unsigned channel)
{
    double value, unused;
    if (source->value) return source->value(source->context, sample, channel);
    source->range(source->context, sample, sample + 1, channel, &value, &unused);
    return value;
}

static double sample_line(const WaveformSource *source, double position, unsigned channel)
{
    size_t a = (size_t)fmin(position, source->samples - 1.0);
    size_t b = a + 1 < source->samples ? a + 1 : a;
    double value = sample_value(source, a, channel);
    return value + (sample_value(source, b, channel) - value) * (position - a);
}

static void include_value(double value, double *low, double *high)
{
    if (value < *low) *low = value;
    if (value > *high) *high = value;
}

/* The times/order of peak-detect extrema are not recorded. Only when a peak
   lies outside the saved sample line, replace that bucket by a continuous
   polygon through its bounds. Endpoints stay fixed; direction chooses the
   order. These vertices are display interpolation, never acquired samples. */
static int peak_piece(const WaveformSource *source, size_t bucket,
                      double left, double right, unsigned channel,
                      double *low, double *high, double *value)
{
    double start = (double)bucket * source->envelope_samples;
    double end = fmin(start + source->envelope_samples, (double)source->samples);
    double raw_low, raw_high, peak_low, peak_high;
    double times[4], values[4];
    size_t first = (size_t)start, limit = (size_t)fmin(end + 1, (double)source->samples);
    unsigned i;
    source->range(source->context, first, limit, channel, &raw_low, &raw_high);
    source->envelope(source->context, bucket, bucket + 1, channel, &peak_low, &peak_high);
    *value = sample_line(source, left, channel);
    *low = *high = *value;
    if (!(peak_low < raw_low || peak_high > raw_high)) {
        size_t a = (size_t)ceil(left), b = (size_t)ceil(right);
        if (b > source->samples) b = source->samples;
        if (a < b) {
            source->range(source->context, a, b, channel, &raw_low, &raw_high);
            include_value(raw_low, low, high); include_value(raw_high, low, high);
        }
        include_value(sample_line(source, right, channel), low, high);
        return 0;
    }
    times[0] = start; times[1] = start + (end - start) / 3;
    times[2] = start + 2 * (end - start) / 3; times[3] = end;
    values[0] = sample_value(source, first, channel);
    values[3] = sample_line(source, end, channel);
    if (peak_low < raw_low && peak_high > raw_high) {
        values[1] = values[3] >= values[0] ? peak_low : peak_high;
        values[2] = values[3] >= values[0] ? peak_high : peak_low;
    } else {
        /* One missing peak must not move the other, already sampled bound.
           Keep the middle sample and place the peak near the closest saved
           extremum. At an endpoint the inset limits the line deviation to
           twice the missing amplitude on a locally linear slope. */
        double middle = (start + end) / 2, mid_value = sample_line(source, middle, channel);
        int missing_low = peak_low < raw_low;
        double target = missing_low ? peak_low : peak_high;
        double delta = missing_low ? raw_low - peak_low : peak_high - raw_high;
        double best = middle, closest = mid_value, offset;
        if (missing_low ? values[0] < closest : values[0] > closest) { best = start; closest = values[0]; }
        if (missing_low ? values[3] < closest : values[3] > closest) { best = end; closest = values[3]; }
        offset = fabs(mid_value - closest);
        offset = offset > 0 ? fmin((end - start) / 3, delta * (end - start) / (2 * offset)) : (end - start) / 3;
        if (best == start) best += offset;
        if (best == end) best -= offset;
        if (delta > raw_high - raw_low) best = middle;
        times[1] = best < middle ? best : middle;
        times[2] = best < middle ? middle : best;
        values[1] = best <= middle ? target : mid_value;
        values[2] = best < middle ? mid_value : target;
    }
    *low = INFINITY; *high = -INFINITY;
    for (i = 0; i < 3; ++i) {
        double a = fmax(left, times[i]), b = fmin(right, times[i + 1]);
        double slope;
        if (times[i + 1] == times[i]) continue;
        slope = (values[i + 1] - values[i]) / (times[i + 1] - times[i]);
        if (a > b) continue;
        double va = values[i] + slope * (a - times[i]);
        double vb = values[i] + slope * (b - times[i]);
        include_value(va, low, high); include_value(vb, low, high);
        if (a == left) *value = va;
    }
    return 1;
}

static void peak_column(const WaveformSource *source, double left, double right,
                        unsigned channel, double *low, double *high, double *value)
{
    double span = (double)source->envelope_samples;
    size_t begin, end;
    double a, b, v, result_low = INFINITY, result_high = -INFINITY, result_value;
    int changed;
    left = fmax(0, left); right = fmin(right, (double)source->samples);
    begin = (size_t)floor(left / span); end = (size_t)floor(right / span);
    if (begin == end) {
        if (peak_piece(source, begin, left, right, channel, &a, &b, &v)) {
            *low = a; *high = b; *value = v;
        }
        return;
    }
    changed = peak_piece(source, begin, left, (begin + 1) * span, channel, &a, &b, &v);
    result_value = v; include_value(a, &result_low, &result_high); include_value(b, &result_low, &result_high);
    ++begin;
    if (begin < end) {
        source->range(source->context, begin * source->envelope_samples,
                      (size_t)fmin(end * span + 1, (double)source->samples), channel, &a, &b);
        include_value(a, &result_low, &result_high); include_value(b, &result_low, &result_high);
        source->envelope(source->context, begin, end, channel, &a, &b);
        changed |= a < result_low || b > result_high;
        include_value(a, &result_low, &result_high); include_value(b, &result_low, &result_high);
    }
    if (right > end * span) {
        changed |= peak_piece(source, end, end * span, right, channel, &a, &b, &v);
        include_value(a, &result_low, &result_high); include_value(b, &result_low, &result_high);
    }
    if (changed) { *low = result_low; *high = result_high; *value = result_value; }
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
            peak_column(source, left, right, channel, &low, &high, &value);
        }
        if (!isfinite(low) || !isfinite(high) || !isfinite(value)) continue;
        minimum[x] = pixel_y(pixels_per_unit >= 0 ? high : low, zero_y, pixels_per_unit);
        maximum[x] = pixel_y(pixels_per_unit >= 0 ? low : high, zero_y, pixels_per_unit);
        representative[x] = pixel_y(value, zero_y, pixels_per_unit);
    }
}
