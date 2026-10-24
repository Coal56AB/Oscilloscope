#include "spectrum_bins.h"
#include "capture_processor.h"
#include "waveform_view.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
static unsigned minimum16(uint8x16_t values)
{
    uint8x8_t v = vpmin_u8(vget_low_u8(values), vget_high_u8(values));
    v = vpmin_u8(v, v); v = vpmin_u8(v, v); v = vpmin_u8(v, v);
    return vget_lane_u8(v, 0);
}
static unsigned maximum16(uint8x16_t values)
{
    uint8x8_t v = vpmax_u8(vget_low_u8(values), vget_high_u8(values));
    v = vpmax_u8(v, v); v = vpmax_u8(v, v); v = vpmax_u8(v, v);
    return vget_lane_u8(v, 0);
}
#endif
static double sample_voltage(const CaptureBuffer *b, size_t sample, unsigned channel)
{
    return ((double)b->data[2 * sample + channel] - b->zero_code[channel]) *
           b->volts_per_code[channel];
}
int capture_find_trigger(const CaptureBuffer *b, unsigned ch, unsigned edge,
                         double volts, unsigned hysteresis_codes,
                         uint64_t first_allowed, uint64_t *sample)
{
    size_t i, start = 1, history;
    double level;
    int previous, rising_armed, falling_armed;
    if (!capture_buffer_valid(b) || ch >= 2 || edge > 2 || !isfinite(volts) || !sample ||
        b->volts_per_code[ch] <= 0) return 0;
    if (first_allowed > b->first_sample) {
        uint64_t offset = first_allowed - b->first_sample;
        if (offset >= b->bytes / 2) return 0;
        if (offset > start) start = (size_t)offset;
    }
    level = volts / b->volts_per_code[ch] + b->zero_code[ch];
    /* Recover the arm state before the allowed trigger window. A noisy return
       across the threshold must not turn a falling slope into a rising trigger. */
    history = start - 1;
    while (history && b->data[2 * history + ch] > level - hysteresis_codes &&
           b->data[2 * history + ch] < level + hysteresis_codes) --history;
    previous = b->data[2 * history + ch];
    rising_armed = previous <= level - hysteresis_codes;
    falling_armed = previous >= level + hysteresis_codes;
    for (i = history + 1; i < b->bytes / 2; ++i) {
        int current = b->data[2 * i + ch];
        int rising = rising_armed && previous <= level && current > level;
        int falling = falling_armed && previous >= level && current < level;
        if (rising) rising_armed = 0;
        if (falling) falling_armed = 0;
        if (current <= level - hysteresis_codes) rising_armed = 1;
        if (current >= level + hysteresis_codes) falling_armed = 1;
        if (i >= start && ((edge != 1 && rising) || (edge != 0 && falling))) {
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
    i = first;
#if defined(__ARM_NEON)
    if (end - i >= 16) {
        uint8x16_t minimum = vdupq_n_u8(255), maximum = vdupq_n_u8(0);
        for (; end - i >= 16; i += 16) {
            uint8x16_t values = vld2q_u8(b->data + 2 * i).val[channel];
            minimum = vminq_u8(minimum, values);
            maximum = vmaxq_u8(maximum, values);
        }
        low = minimum16(minimum); high = maximum16(maximum);
    }
#endif
    for (; i < end; ++i) {
        unsigned value = b->data[2 * i + channel];
        if (value < low) low = value;
        if (value > high) high = value;
    }
    *minimum = (low - b->zero_code[channel]) * b->volts_per_code[channel];
    *maximum = (high - b->zero_code[channel]) * b->volts_per_code[channel];
    if (*minimum > *maximum) { double swap = *minimum; *minimum = *maximum; *maximum = swap; }
}
static void source_envelope(const void *context, size_t first, size_t end, unsigned channel,
                            double *minimum, double *maximum)
{
    const CaptureBuffer *b = context;
    unsigned low = 255, high = 0;
    size_t i;
    for (i = first; i < end; ++i) {
        if (b->peaks[4*i+channel] < low) low = b->peaks[4*i+channel];
        if (b->peaks[4*i+2+channel] > high) high = b->peaks[4*i+2+channel];
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
static double source_value(const void *context,size_t sample,unsigned channel)
{
    return sample_voltage(context,sample,channel);
}
/* Use the available record up to 64K contiguous samples without decimation.
   Heap workspace keeps the larger transform off the GUI/worker stack. */
static int spectrum(const CaptureBuffer *b, const CaptureView *v, unsigned ch, unsigned n, uint8_t *bins)
{
    double *re = malloc(2 * n * sizeof(*re)), *im;
    unsigned i, j, length;
    double gain = fabs(b->volts_per_code[ch]) * 128, mean = 0;
    unsigned count = v->samples < n ? (unsigned)v->samples : n;
    if (!re) return 0;
    im = re + n;
    for (i = 0; i < count; ++i) mean += sample_voltage(b, v->start + i, ch);
    if (count) mean /= count;
    for (i = 0; i < n; ++i) {
        size_t offset = i;
        double value = 0;
        if (offset < v->samples)
            value = sample_voltage(b, v->start + offset, ch) - mean;
        re[i] = value * (0.5 - 0.5 * cos(6.283185307179586 * i / (n - 1)));
        im[i] = 0;
    }
    for (i = 1, j = 0; i < n; ++i) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
        }
    }
    for (length = 2; length <= n; length <<= 1) {
        unsigned half = length / 2, k;
        for (k = 0; k < half; ++k) {
            double angle = -6.283185307179586 * k / length;
            double c = cos(angle), s = sin(angle);
            for (i = 0; i < n; i += length) {
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
        spectrum_bin_range(i, SCOPE_FFT_BINS, n, &first, &end);
        for (k = first; k < end; ++k) {
            double value = hypot(re[k], im[k]) * 4 / n;
            if (value > magnitude) magnitude = value;
        }
        double db = gain > 0 ? 20 * log10(magnitude / gain + 1e-12) : -120;
        double y = spectrum_level_at_db(db);
        bins[i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
    }
    free(re);
    return 1;
}
/* Count original ADC hits before reduction. Lookup tables avoid floating point
   work per sample; the retained raw record remains the source for STOP/ZOOM. */
static int density(const CaptureBuffer *b, const CaptureView *v, double first,
                   double samples, unsigned ch, uint8_t *levels)
{
    size_t x, i;
    unsigned peak = 0, code;
    unsigned height=v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
    int rows[256];
    uint16_t *counts = calloc(SCOPE_PLOT_WIDTH * SCOPE_PLOT_HEIGHT, sizeof(*counts));
    uint8_t *palette;
    if (!counts) return 0;
    for (code = 0; code < 256; ++code) {
        double y = (v->zero_y[ch] - (code - b->zero_code[ch]) * b->volts_per_code[ch] * v->pixels_per_volt[ch]) * height/SCOPE_PLOT_HEIGHT;
        rows[code] = y >= 0 && y < height ? (int)y : -1;
    }
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        size_t a = (size_t)ceil(fmax(0, fmin(first + x * samples / SCOPE_PLOT_WIDTH, b->bytes / 2)));
        size_t end = (size_t)ceil(fmax(0, fmin(first + (x + 1) * samples / SCOPE_PLOT_WIDTH, b->bytes / 2)));
        for (i = a; i < end; ++i) {
            int y = rows[b->data[2*i+ch]];
            if (y >= 0) {
                uint16_t *count = &counts[x*SCOPE_PLOT_HEIGHT+y];
                if (*count < UINT16_MAX) ++*count;
                if (*count > peak) peak = *count;
            }
        }
    }
    palette = malloc(peak + 1);
    if (!palette) {free(counts);return 0;}
    for (code = 0; code <= peak; ++code) palette[code] = (uint8_t)waveform_density_level(code,peak);
    for (i = 0; i < SCOPE_PLOT_WIDTH * SCOPE_PLOT_HEIGHT; ++i) levels[i] = palette[counts[i]];
    free(palette);free(counts);
    return 1;
}

int capture_process(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f)
{
    unsigned ch;
    WaveformSource source={0};
    CaptureView overlap;
    double display_first, display_samples;
    uint64_t start_ns = scope_clock_ns();
    if (!capture_buffer_valid(b) || !v || !f)
        return 0;
    if(v->density_rows>SCOPE_PLOT_HEIGHT)return 0;
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
    source.value = b->peaks?source_value:NULL;
    source.envelope = b->peaks?source_envelope:NULL;
    source.envelope_samples = b->peaks?2:0;
    memset(f, 0, offsetof(DisplayFrame,connections));
    f->sequence = b->sequence;
    f->sample_rate_hz = b->sample_rate_hz;
    f->peak_detect = b->peaks!=NULL;
    f->display_samples = display_samples;
    f->fft_size = 1024;
    while (f->fft_size < 65536 && f->fft_size * 2 <= v->samples) f->fft_size *= 2;
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
        i = 0;
#if defined(__ARM_NEON)
        /* Bound each vector accumulator so squares cannot overflow uint32.
           Scalar tails and non-NEON builds use the same exact code sums. */
        while (v->samples - i >= 16) {
            size_t end = i + ((v->samples - i) & ~(size_t)15);
            uint8x16_t minimum = vdupq_n_u8(255), maximum = vdupq_n_u8(0);
            uint32x4_t vector_sum = vdupq_n_u32(0), vector_squares = vdupq_n_u32(0);
            unsigned lane;
            uint32_t sums[4], powers[4];
            if (end - i > 65536) end = i + 65536;
            for (; i < end; i += 16) {
                uint8x16_t values = vld2q_u8(b->data + 2 * (v->start + i)).val[ch];
                minimum = vminq_u8(minimum, values);
                maximum = vmaxq_u8(maximum, values);
                vector_sum = vpadalq_u16(vector_sum, vpaddlq_u8(values));
                vector_squares = vpadalq_u16(vector_squares,
                    vmull_u8(vget_low_u8(values), vget_low_u8(values)));
                vector_squares = vpadalq_u16(vector_squares,
                    vmull_u8(vget_high_u8(values), vget_high_u8(values)));
            }
            vst1q_u32(sums, vector_sum); vst1q_u32(powers, vector_squares);
            for (lane = 0; lane < 4; ++lane) { sum += sums[lane]; squares += powers[lane]; }
            if (minimum16(minimum) < low) low = minimum16(minimum);
            if (maximum16(maximum) > peak) peak = maximum16(maximum);
        }
#endif
        for (; i < v->samples; ++i) {
            unsigned value = b->data[2 * (v->start + i) + ch];
            if (value < low) low = value;
            if (value > peak) peak = value;
            sum += value;
            squares += value * value;
        }
        f->minimum[ch] = v->samples ? ((gain >= 0 ? low : peak) - zero) * gain : NAN;
        f->maximum[ch] = v->samples ? ((gain >= 0 ? peak : low) - zero) * gain : NAN;
        if (b->peaks && v->samples) {
            double envelope_low, envelope_high;
            source_envelope(b, v->start/2, (v->start+v->samples+1)/2, ch,
                             &envelope_low, &envelope_high);
            if (envelope_low < f->minimum[ch]) f->minimum[ch] = envelope_low;
            if (envelope_high > f->maximum[ch]) f->maximum[ch] = envelope_high;
        }
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
            i = 0;
#if defined(__ARM_NEON)
            {
                uint32x4_t highs = vdupq_n_u32(0), edges = vdupq_n_u32(0);
                uint8x16_t bound = vdupq_n_u8((threshold + (gain > 0)) / 2);
                uint8x16_t last_edges = vdupq_n_u8(0);
                size_t last_base = 0;
                int found = 0;
                unsigned lane;
                uint32_t counts[4];
                for (; v->samples - i >= 16; i += 16) {
                    uint8x16_t values = vld2q_u8(b->data + 2 * (v->start + i)).val[ch];
                    uint8x16_t current = gain == 0 ? vdupq_n_u8(255) :
                        gain > 0 ? vcgeq_u8(values, bound) : vcleq_u8(values, bound);
                    uint8x16_t previous = vextq_u8(vdupq_n_u8(previous_high ? 255 : 0), current, 15);
                    uint8x16_t rising = vbicq_u8(current, previous);
                    uint64x2_t bits = vreinterpretq_u64_u8(rising);
                    highs = vpadalq_u16(highs, vpaddlq_u8(vandq_u8(current, vdupq_n_u8(1))));
                    edges = vpadalq_u16(edges, vpaddlq_u8(vandq_u8(rising, vdupq_n_u8(1))));
                    if (vgetq_lane_u64(bits, 0) | vgetq_lane_u64(bits, 1)) {
                        if (!found) {
                            uint8_t flags[16];
                            vst1q_u8(flags, rising);
                            for (lane = 0; lane < 16 && !flags[lane]; ++lane) {}
                            first = i + lane; found = 1;
                        }
                        last_edges = rising; last_base = i;
                    }
                    previous_high = vgetq_lane_u8(current, 15) != 0;
                }
                vst1q_u32(counts, highs);
                for (lane = 0; lane < 4; ++lane) high += counts[lane];
                vst1q_u32(counts, edges);
                for (lane = 0; lane < 4; ++lane) crossings += counts[lane];
                if (found) {
                    uint8_t flags[16];
                    vst1q_u8(flags, last_edges);
                    for (lane = 0; lane < 16; ++lane) if (flags[lane]) last = last_base + lane;
                }
            }
#endif
            for (; i < v->samples; ++i) {
                unsigned value = 2u * b->data[2 * (v->start + i) + ch];
                int current_high = gain == 0 || (gain > 0 ? value >= threshold : value <= threshold);
                unsigned rising = (unsigned)(!previous_high && current_high);
                size_t mask = (size_t)0 - rising;
                size_t first_mask = mask & ((size_t)0 - (crossings == 0));
                /* Noise crosses the threshold unpredictably. Select positions
                   without a branch for every crossing. */
                first = (first & ~first_mask) | (i & first_mask);
                last = (last & ~mask) | (i & mask);
                crossings += rising;
                high += current_high;
                previous_high = current_high;
            }
        }
        if (crossings > 1 && last > first)
            f->frequency_hz[ch] = (double)(crossings - 1) * b->sample_rate_hz / (last - first);
        f->duty[ch] = v->samples ? 100.0 * high / v->samples : NAN;
        waveform_view_reduce(&source, display_first, display_samples, ch,
                             v->zero_y[ch], v->pixels_per_volt[ch], SCOPE_PLOT_WIDTH,
                             f->y[ch], f->y_min[ch], f->y_max[ch]);
        waveform_view_peak_bounds(&source, display_first, display_samples, ch,
                                 v->zero_y[ch], v->pixels_per_volt[ch], fabs(gain),
                                 SCOPE_PLOT_WIDTH, f->peak_min[ch], f->peak_max[ch], f->peak_strength[ch]);
        if (!v->skip_spectrum && !spectrum(b, v, ch, f->fft_size, f->fft[ch])) return 0;
        if (v->density_enabled && !density(b,v,display_first,display_samples,ch,&f->density[ch][0][0])) return 0;
        if(v->density_enabled){
            double positions[256];unsigned code;
            unsigned rows=v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
            for(code=0;code<256;++code)
                positions[code]=(v->zero_y[ch]-(code-b->zero_code[ch])*gain*v->pixels_per_volt[ch])*rows/SCOPE_PLOT_HEIGHT;
            memset(f->connections[ch],0,sizeof(f->connections[ch]));
            if(!waveform_view_connect_u8(b->data+ch,b->bytes/2,2,positions,display_first,display_samples,
                                         SCOPE_PLOT_WIDTH,rows,&f->connections[ch][0][0]))return 0;
        }
    }
    f->density_valid = v->density_enabled;
    f->density_rows = v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
    f->processing_ns = scope_clock_ns() - start_ns;
    return 1;
}
