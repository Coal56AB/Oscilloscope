#include "spectrum_bins.h"
#include "capture_processor.h"
#include "waveform_view.h"
#include <math.h>
#include <string.h>
static double sample_voltage(const CaptureBuffer *b, size_t sample, unsigned channel)
{
    return ((double)b->data[2 * sample + channel] - b->zero_code[channel]) *
           b->volts_per_code[channel];
}
int capture_find_trigger(const CaptureBuffer *b, unsigned ch, unsigned edge,
                         double volts, uint64_t first_allowed, uint64_t *sample)
{
    size_t i, start = 1;
    double level;
    int previous;
    if (!capture_buffer_valid(b) || ch >= 2 || edge > 2 || !isfinite(volts) || !sample ||
        b->volts_per_code[ch] <= 0) return 0;
    if (first_allowed > b->first_sample) {
        uint64_t offset = first_allowed - b->first_sample;
        if (offset >= b->bytes / 2) return 0;
        if (offset > start) start = (size_t)offset;
    }
    level = volts / b->volts_per_code[ch] + b->zero_code[ch];
    previous = b->data[2 * (start - 1) + ch];
    for (i = start; i < b->bytes / 2; ++i) {
        int current = b->data[2 * i + ch];
        if ((edge != 1 && previous <= level && current > level) ||
            (edge != 0 && previous >= level && current < level)) {
            *sample = b->first_sample + i;
            return 1;
        }
        previous = current;
    }
    return 0;
}
static void source_range(const void *context, size_t first, size_t end, unsigned channel,
                         double *minimum, double *maximum)
{
    const CaptureBuffer *b = context;
    size_t i;
    unsigned low = b->data[2 * first + channel], high = low;
    for (i = first + 1; i < end; ++i) {
        unsigned value = b->data[2 * i + channel];
        if (value < low) low = value;
        if (value > high) high = value;
    }
    *minimum = (low - b->zero_code[channel]) * b->volts_per_code[channel];
    *maximum = (high - b->zero_code[channel]) * b->volts_per_code[channel];
    if (*minimum > *maximum) { double swap = *minimum; *minimum = *maximum; *maximum = swap; }
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
    double gain = fabs(b->volts_per_code[ch]) * 128, mean = 0;
    unsigned count = v->samples < N ? (unsigned)v->samples : N;
    for (i = 0; i < count; ++i) mean += sample_voltage(b, v->start + i, ch);
    if (count) mean /= count;
    for (i = 0; i < N; ++i) {
        size_t offset = i;
        double value = 0;
        if (offset < v->samples)
            value = sample_voltage(b, v->start + offset, ch) - mean;
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
        for (k = 0; k < half; ++k) {
            double angle = -6.283185307179586 * k / length;
            double c = cos(angle), s = sin(angle);
            for (i = 0; i < N; i += length) {
                double tr = c * re[i + k + half] - s * im[i + k + half],
                       ti = s * re[i + k + half] + c * im[i + k + half];
                re[i + k + half] = re[i + k] - tr;
                im[i + k + half] = im[i + k] - ti;
                re[i + k] += tr;
                im[i + k] += ti;
            }
        }
    }
    for (i = 0; i < SCOPE_FFT_BINS; ++i) {
        unsigned first, end, k;
        double magnitude = 0;
        spectrum_bin_range(i, SCOPE_FFT_BINS, N, &first, &end);
        for (k = first; k < end; ++k) {
            double value = hypot(re[k], im[k]) * 4 / N;
            if (value > magnitude) magnitude = value;
        }
        double db = gain > 0 ? 20 * log10(magnitude / gain + 1e-12) : -120;
        double y = spectrum_level_at_db(db);
        bins[i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
    }
}
int capture_process(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f)
{
    unsigned ch;
    WaveformSource source;
    CaptureView overlap;
    double display_first, display_samples;
    uint64_t start_ns = scope_clock_ns();
    if (!capture_buffer_valid(b) || !v || !f)
        return 0;
    if (v->display_samples != 0) {
        double first, end;
        if (!isfinite(v->display_first) || !isfinite(v->display_samples) || v->display_samples <= 0)
            return 0;
        display_first=v->display_first;display_samples=v->display_samples;
        first=fmax(0.0,fmin(display_first,b->bytes/2));
        end=fmax(0.0,fmin(display_first+display_samples,b->bytes/2));
        overlap=*v;
        overlap.start=(size_t)floor(first);
        overlap.samples=end>first?(size_t)ceil(end)-overlap.start:0;
        v=&overlap;
    } else {
        if (!v->samples || v->start >= b->bytes / 2 || v->samples > b->bytes / 2 - v->start)
            return 0;
        display_first=(double)v->start;display_samples=(double)v->samples;
    }
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(b->volts_per_code[ch]) || !isfinite(b->zero_code[ch]) ||
            !isfinite(v->pixels_per_volt[ch]) || !isfinite(v->zero_y[ch]))
            return 0;
    source.context = b;
    source.samples = b->bytes / 2;
    source.range = source_range;
    memset(f, 0, sizeof(*f));
    f->sequence = b->sequence;
    f->sample_rate_hz = b->sample_rate_hz;
    f->trigger_sample = b->trigger_sample;
    f->trigger_x = b->trigger_sample != CAPTURE_NO_TRIGGER && b->trigger_sample >= b->first_sample ?
        ((double)(b->trigger_sample - b->first_sample) - display_first) * SCOPE_PLOT_WIDTH / display_samples :
        SCOPE_PLOT_WIDTH / 2.0;
    f->view_start = v->start;
    f->view_samples = v->samples;
    for (ch = 0; ch < 2; ++ch) {
        uint64_t sum = 0, squares = 0;
        size_t i, first = 0, last = 0, crossings = 0, high = 0;
        unsigned low = 255, peak = 0;
        double gain = b->volts_per_code[ch], zero = b->zero_code[ch];
        for (i = 0; i < v->samples; ++i) {
            unsigned value = b->data[2 * (v->start + i) + ch];
            if (value < low) low = value;
            if (value > peak) peak = value;
            sum += value;
            squares += value * value;
        }
        f->minimum[ch] = v->samples ? ((gain >= 0 ? low : peak) - zero) * gain : NAN;
        f->maximum[ch] = v->samples ? ((gain >= 0 ? peak : low) - zero) * gain : NAN;
        if (v->samples) {
            double mean = (double)sum / v->samples;
            f->mean[ch] = (mean - zero) * gain;
            f->rms[ch] = sqrt(fmax(0, (double)squares / v->samples - 2 * zero * mean + zero * zero)) * fabs(gain);
        } else f->mean[ch] = f->rms[ch] = NAN;
        {
            unsigned threshold = low + peak;
            int previous_high = v->samples ? (gain == 0 || (gain > 0 ?
                2u * b->data[2 * v->start + ch] >= threshold :
                2u * b->data[2 * v->start + ch] <= threshold)) : 0;
            for (i = 0; i < v->samples; ++i) {
                unsigned value = 2u * b->data[2 * (v->start + i) + ch];
                int current_high = gain == 0 || (gain > 0 ? value >= threshold : value <= threshold);
                if (current_high) ++high;
                if (!previous_high && current_high) {
                    if (!crossings)
                        first = i;
                    last = i;
                    ++crossings;
                }
                previous_high = current_high;
            }
        }
        if (crossings > 1 && last > first)
            f->frequency_hz[ch] = (double)(crossings - 1) * b->sample_rate_hz / (last - first);
        f->duty[ch] = v->samples ? 100.0 * high / v->samples : NAN;
        waveform_view_reduce(&source, display_first, display_samples, ch,
                             v->zero_y[ch], v->pixels_per_volt[ch], SCOPE_PLOT_WIDTH,
                             f->y[ch], f->y_min[ch], f->y_max[ch]);
        if (!v->skip_spectrum) spectrum(b, v, ch, f->fft[ch]);
    }
    f->processing_ns = scope_clock_ns() - start_ns;
    return 1;
}
