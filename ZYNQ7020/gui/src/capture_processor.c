#include "capture_processor.h"
#include <math.h>
#include <string.h>
static int16_t pixel_y(double volts, double zero, double scale)
{
    double y = zero - volts * scale;
    return (int16_t)(y < 0 ? 0 : y > SCOPE_PLOT_HEIGHT - 1 ? SCOPE_PLOT_HEIGHT - 1 : y);
}
static double sample_voltage(const CaptureBuffer *b, size_t sample, unsigned channel)
{
    return ((double)b->data[2 * sample + channel] - b->zero_code[channel]) *
           b->volts_per_code[channel];
}
int capture_value_at(const CaptureBuffer *b, size_t sample, unsigned channel, double *volts)
{
    if (!capture_buffer_valid(b) || sample >= b->bytes / 2 || channel >= 2 || !volts)
        return 0;
    *volts = sample_voltage(b, sample, channel);
    return 1;
}
/* Fixed-size radix-2 FFT; no allocation and no dependence on raw record depth. */
static void spectrum(const CaptureBuffer *b, const CaptureView *v, unsigned ch, uint8_t *bins)
{
    enum { N = 1024 };
    double re[N], im[N];
    unsigned i, j, length;
    double gain = fabs(b->volts_per_code[ch]) * 128;
    for (i = 0; i < N; ++i) {
        size_t offset = i;
        double value = 0;
        if (offset < v->samples)
            value = sample_voltage(b, v->start + offset, ch);
        re[i] = value * (0.5 - 0.5 * cos(6.283185307179586 * i / (N - 1)));
        im[i] = 0;
    }
    for (i = 1, j = 0; i < N; ++i) {
        unsigned bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
        }
    }
    for (length = 2; length <= N; length <<= 1) {
        unsigned half = length / 2, k;
        for (i = 0; i < N; i += length)
            for (k = 0; k < half; ++k) {
                double angle = -6.283185307179586 * k / length;
                double c = cos(angle), s = sin(angle),
                       tr = c * re[i + k + half] - s * im[i + k + half],
                       ti = s * re[i + k + half] + c * im[i + k + half];
                re[i + k + half] = re[i + k] - tr;
                im[i + k + half] = im[i + k] - ti;
                re[i + k] += tr;
                im[i + k] += ti;
            }
    }
    for (i = 0; i < SCOPE_FFT_BINS; ++i) {
        unsigned k = i * (N / 2) / SCOPE_FFT_BINS;
        double magnitude = hypot(re[k], im[k]) * 4 / N;
        double db = gain > 0 ? 20 * log10(magnitude / gain + 1e-12) : -120;
        double y = (db + 80) * 255 / 80;
        bins[i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
    }
}
int capture_process(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f)
{
    unsigned ch;
    size_t x;
    uint64_t start_ns = scope_clock_ns();
    if (!capture_buffer_valid(b) || !v || !f || !v->samples || v->start >= b->bytes / 2 ||
        v->samples > b->bytes / 2 - v->start)
        return 0;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(b->volts_per_code[ch]) || !isfinite(b->zero_code[ch]) ||
            !isfinite(v->pixels_per_volt[ch]) || !isfinite(v->zero_y[ch]))
            return 0;
    memset(f, 0, sizeof(*f));
    f->sequence = b->sequence;
    f->sample_rate_hz = b->sample_rate_hz;
    f->view_start = v->start;
    f->view_samples = v->samples;
    for (ch = 0; ch < 2; ++ch) {
        double sum = 0, squares = 0;
        size_t i, first = 0, last = 0, crossings = 0, high = 0;
        double first_value = sample_voltage(b, v->start, ch), previous = first_value;
        f->minimum[ch] = f->maximum[ch] = first_value;
        for (i = 0; i < v->samples; ++i) {
            double value = sample_voltage(b, v->start + i, ch);
            if (value < f->minimum[ch])
                f->minimum[ch] = value;
            if (value > f->maximum[ch])
                f->maximum[ch] = value;
            sum += value;
            squares += value * value;
        }
        f->mean[ch] = sum / v->samples;
        f->rms[ch] = sqrt(squares / v->samples);
        {
            double threshold = (f->maximum[ch] + f->minimum[ch]) * 0.5;
            for (i = 0; i < v->samples; ++i) {
                double value = sample_voltage(b, v->start + i, ch);
                if (value >= threshold)
                    ++high;
                if (previous < threshold && value >= threshold) {
                    if (!crossings)
                        first = i;
                    last = i;
                    ++crossings;
                }
                previous = value;
            }
        }
        if (crossings > 1 && last > first)
            f->frequency_hz[ch] = (double)(crossings - 1) * b->sample_rate_hz / (last - first);
        f->duty[ch] = 100.0 * high / v->samples;
        for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
            size_t a = x * (v->samples / SCOPE_PLOT_WIDTH) +
                       x * (v->samples % SCOPE_PLOT_WIDTH) / SCOPE_PLOT_WIDTH;
            size_t end = (x + 1) * (v->samples / SCOPE_PLOT_WIDTH) +
                         (x + 1) * (v->samples % SCOPE_PLOT_WIDTH) / SCOPE_PLOT_WIDTH;
            double low, upper, value;
            if (end <= a)
                end = a + 1;
            if (end > v->samples)
                end = v->samples;
            low = sample_voltage(b, v->start + a, ch);
            upper = low;
            for (i = a + 1; i < end; ++i) {
                value = sample_voltage(b, v->start + i, ch);
                if (value < low)
                    low = value;
                if (value > upper)
                    upper = value;
            }
            f->y_min[ch][x] = pixel_y(upper, v->zero_y[ch], v->pixels_per_volt[ch]);
            f->y_max[ch][x] = pixel_y(low, v->zero_y[ch], v->pixels_per_volt[ch]);
            f->y[ch][x] = pixel_y((low + upper) * 0.5, v->zero_y[ch], v->pixels_per_volt[ch]);
        }
        spectrum(b, v, ch, f->fft[ch]);
    }
    f->processing_ns = scope_clock_ns() - start_ns;
    return 1;
}
