#include "spectrum_bins.h"
#include "dsp_fft.h"
#include "capture_processor.h"
#include "waveform_view.h"
#include "signal_filter.h"
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
/* Adjacent-code jitter sets the noise guard; limit it to a quarter of the
   observed excursion so resolved fast edges remain eligible. No samples are
   filtered or replaced, and the result is still an original level crossing. */
static double trigger_code(const CaptureBuffer *b,unsigned ch,size_t i,const double *filtered)
{
    return filtered?filtered[i]:b->data[2*i+ch];
}
static unsigned trigger_noise_guard(const CaptureBuffer *b,unsigned ch,double level,unsigned minimum,const double *filtered)
{
    unsigned differences[256]={0},median=0;
    double low=255,high=0;
    size_t count=b->bytes/2,i,total=0;
    if(!minimum || count<32)return minimum;
    for(i=0;i<count;++i){
        double value=trigger_code(b,ch,i,filtered);
        if(value<low)low=value;if(value>high)high=value;
        if(i){unsigned delta=(unsigned)floor(fabs(value-trigger_code(b,ch,i-1,filtered))+.5);++differences[delta>255?255:delta];}
    }
    for(;median<255;++median){total+=differences[median];if(total>=count/2)break;}
    unsigned guard=median*3,limit=(unsigned)((high-low)/4);
    if(level>low && level<high){
        unsigned headroom=(unsigned)fmin(level-low,high-level);
        if(limit>headroom)limit=headroom;
    }
    if(guard>limit)guard=limit;
    return guard>minimum?guard:minimum;
}

static int find_trigger(const CaptureBuffer *b, unsigned ch, unsigned edge,
                         double volts, unsigned hysteresis_codes,
                         uint64_t first_allowed, uint64_t *sample,const double *filtered)
{
    size_t i, start = 1, history;
    double level;
    double previous;int state;
    size_t crossing=SIZE_MAX;
    if (!capture_buffer_valid(b) || ch >= 2 || edge > 2 || !isfinite(volts) || !sample ||
        b->volts_per_code[ch] <= 0) return 0;
    if (first_allowed > b->first_sample) {
        uint64_t offset = first_allowed - b->first_sample;
        if (offset >= b->bytes / 2) return 0;
        if (offset > start) start = (size_t)offset;
    }
    level = volts / b->volts_per_code[ch] + b->zero_code[ch];
    hysteresis_codes=trigger_noise_guard(b,ch,level,hysteresis_codes,filtered);
    /* Recover both the source side and any already-started transition. A
       crossing is accepted only after reaching the opposite side of the band. */
    history = start - 1;
    while (history && trigger_code(b,ch,history,filtered) > level - hysteresis_codes &&
           trigger_code(b,ch,history,filtered) < level + hysteresis_codes) --history;
    previous = trigger_code(b,ch,history,filtered);
    state=previous<=level-hysteresis_codes?-1:previous>=level+hysteresis_codes?1:0;
    for (i = history + 1; i < b->bytes / 2; ++i) {
        double current = trigger_code(b,ch,i,filtered);
        if(!hysteresis_codes){
            if(i>=start && ((edge!=1 && previous<=level && current>level) ||
                           (edge!=0 && previous>=level && current<level))){
                *sample=b->first_sample+i;return 1;
            }
        }else{
            if(crossing==SIZE_MAX && ((state<0 && previous<=level && current>level) ||
                                     (state>0 && previous>=level && current<level)))crossing=i;
            int side=current<=level-hysteresis_codes?-1:current>=level+hysteresis_codes?1:0;
            if(side){
                if(state && side!=state && crossing!=SIZE_MAX && crossing>=start &&
                   ((side>0 && edge!=1) || (side<0 && edge!=0))){
                    *sample=b->first_sample+crossing;return 1;
                }
                state=side;crossing=SIZE_MAX;
            }
        }
        previous = current;
    }
    return 0;
}
int capture_find_trigger(const CaptureBuffer *b,unsigned ch,unsigned edge,double volts,unsigned hysteresis,
                         uint64_t first_allowed,uint64_t *sample)
{
    return find_trigger(b,ch,edge,volts,hysteresis,first_allowed,sample,NULL);
}
int capture_find_trigger_filtered(const CaptureBuffer *b,unsigned ch,unsigned edge,double volts,unsigned hysteresis,
                                  unsigned filter,double noise_rms,uint64_t first_allowed,uint64_t *sample)
{
    double *workspace;size_t count,i;int found;
    if(!isfinite(noise_rms) || noise_rms<0)return 0;
    if(!filter && noise_rms<=0)return capture_find_trigger(b,ch,edge,volts,hysteresis,first_allowed,sample);
    if(!capture_buffer_valid(b) || ch>=2 || filter>3 || b->bytes/2>SIZE_MAX/(2*sizeof(double)))return 0;
    count=b->bytes/2;workspace=malloc(count*2*sizeof(double));if(!workspace)return 0;
    for(i=0;i<count;++i)workspace[i]=b->data[2*i+ch];
    double *input=workspace,*output=workspace+count,*swap;
    if(noise_rms>0){signal_filter_denoise(input,count,noise_rms/b->volts_per_code[ch],output);swap=input;input=output;output=swap;}
    if(filter){signal_filter_apply(input,count,filter,output);input=output;}
    found=find_trigger(b,ch,edge,volts,hysteresis,first_allowed,sample,input);
    free(workspace);return found;
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
static int spectrum(const CaptureBuffer *b, const CaptureView *v, unsigned ch, unsigned n, uint8_t *bins,double *frequency,double *amplitude)
{
    double *re = malloc(2 * n * sizeof(*re)), *im;
    unsigned i;
    double gain = fabs(b->volts_per_code[ch]) * 128, mean = 0;
    unsigned count = v->samples < n ? (unsigned)v->samples : n;
    if (!re) return 0;
    im = re + n;
    for (i = 0; i < count; ++i) mean += v->corrected[ch]?v->corrected[ch][v->start+i]:sample_voltage(b, v->start + i, ch);
    if (count) mean /= count;
    for (i = 0; i < n; ++i) {
        size_t offset = i;
        double value = 0;
        if (offset < v->samples)
            value = (v->corrected[ch]?v->corrected[ch][v->start+offset]:sample_voltage(b, v->start + offset, ch)) - mean;
        re[i] = value * (0.5 - 0.5 * cos(6.283185307179586 * i / (n - 1)));
        im[i] = 0;
    }
    dsp_fft(re,im,n,0);
    for (i = 0; i < SCOPE_FFT_BINS; ++i) {
        double magnitude = spectrum_project(re,im,n,i,SCOPE_FFT_BINS)*4/n;
        double db = gain > 0 ? 20 * log10(magnitude / gain + 1e-12) : -120;
        double y = spectrum_level_at_db(db);
        bins[i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
    }
    {
        unsigned peak=1,k;double delta=0,log_peak;
        for(k=2;k<=n/2;++k)if(hypot(re[k],im[k])>hypot(re[peak],im[peak]))peak=k;
        log_peak=log(hypot(re[peak],im[peak])+1e-30);
        if(peak>1 && peak<n/2){
            double a=log(hypot(re[peak-1],im[peak-1])+1e-30);
            double c=log(hypot(re[peak+1],im[peak+1])+1e-30);
            double denominator=a-2*log_peak+c;
            if(denominator<0){delta=0.5*(a-c)/denominator;log_peak-=0.25*(a-c)*delta;}
        }
        *frequency=(peak+delta)*b->sample_rate_hz/n;
        *amplitude=exp(log_peak)*4/n;
    }
    free(re);
    return 1;
}
/* Count original ADC hits before reduction. Lookup tables avoid floating point
   work per sample; the retained raw record remains the source for STOP/ZOOM. */
static int density(const CaptureBuffer *b, const CaptureView *v, double first,
                   double samples, unsigned ch, const double *filtered, uint8_t *levels)
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
            int y;
            if (filtered) {
                double position = (v->zero_y[ch] - filtered[i] * v->pixels_per_volt[ch]) * height/SCOPE_PLOT_HEIGHT;
                y = position >= 0 && position < height ? (int)position : -1;
            } else y = rows[b->data[2*i+ch]];
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

static double filtered_value(const void *context, size_t sample, unsigned channel)
{
    (void)channel;
    return ((const double *)context)[sample];
}

static void filtered_range(const void *context, size_t first, size_t end, unsigned channel,
                            double *minimum, double *maximum)
{
    const double *values = context;
    size_t i;
    (void)channel;
    *minimum = *maximum = values[first];
    for (i = first + 1; i < end; ++i) {
        if (values[i] < *minimum) *minimum = values[i];
        if (values[i] > *maximum) *maximum = values[i];
    }
}

/* Filtering precedes every display representation. Optional response correction
   is a separate derived record; the immutable ADC input still drives triggering. */
typedef struct { const CaptureBuffer *buffer;const double *values;double noise; } AutoEnvelope;
static double auto_value(const void *context,size_t sample,unsigned channel)
{
    return filtered_value(((const AutoEnvelope *)context)->values,sample,channel);
}
static void auto_range(const void *context,size_t first,size_t end,unsigned channel,double *low,double *high)
{
    filtered_range(((const AutoEnvelope *)context)->values,first,end,channel,low,high);
}
static void auto_envelope(const void *context,size_t first,size_t end,unsigned channel,double *low,double *high)
{
    const AutoEnvelope *source=context;
    source_envelope(source->buffer,first,end,channel,low,high);
    /* Retain unresolved signal excursions, suppress only the measured noise
       envelope. Never replace a real fast square wave with its decimated base. */
    if(*high-*low<=8*source->noise){
        size_t last=end*2,count=source->buffer->bytes/2;
        if(last>count)last=count;
        filtered_range(source->values,first*2,last,channel,low,high);
    }
}
static int render_channel(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f,
                           unsigned ch, double first, double samples)
{
    WaveformSource source = {0};
    size_t count = b->bytes / 2, i;
    double *workspace = NULL;
    AutoEnvelope auto_source={0};
    const double *filtered = v->corrected[ch];
    int ok = 1;
    unsigned rows = v->density_rows ? v->density_rows : SCOPE_PLOT_HEIGHT;
    source.samples = count;
    source.context = b;
    source.range = source_range;
    source.value = b->peaks ? source_value : NULL;
    source.envelope = b->peaks ? source_envelope : NULL;
    source.envelope_samples = b->peaks ? 2 : 0;
    if(filtered){
        source.context=(void *)filtered;source.range=filtered_range;source.value=filtered_value;
        source.envelope=NULL;source.envelope_samples=0;
    }
    if (v->filter_level[ch] || v->noise_rms[ch]>0) {
        if (count > SIZE_MAX / (2 * sizeof(*workspace))) return 0;
        workspace = malloc(2 * count * sizeof(*workspace));
        if (!workspace) return 0;
        for (i = 0; i < count; ++i) workspace[i] = v->corrected[ch]?v->corrected[ch][i]:sample_voltage(b, i, ch);
        double *input=workspace,*output=workspace+count,*swap;
        if(v->noise_rms[ch]>0){signal_filter_denoise(input,count,v->noise_rms[ch],output);swap=input;input=output;output=swap;}
        if(v->filter_level[ch]){signal_filter_apply(input,count,v->filter_level[ch],output);input=output;}
        filtered=input;
        source.context = filtered;
        source.range = filtered_range;
        source.value = filtered_value;
        /* Peak buckets have no known extrema timestamps; they cannot pass
           through a time-domain filter or overlay its smoothed result. */
        source.envelope = NULL;
        source.envelope_samples = 0;
        if(b->peaks && !v->filter_level[ch] && !v->corrected[ch]){
            auto_source=(AutoEnvelope){b,filtered,v->noise_rms[ch]};
            source.context=&auto_source;source.range=auto_range;source.value=auto_value;
            source.envelope=auto_envelope;source.envelope_samples=2;
        }
    }
    waveform_view_reduce(&source, first, samples, ch, v->zero_y[ch], v->pixels_per_volt[ch],
                         SCOPE_PLOT_WIDTH, f->y[ch], f->y_min[ch], f->y_max[ch]);
    waveform_view_peak_bounds(&source, first, samples, ch, v->zero_y[ch], v->pixels_per_volt[ch],
                             fabs(b->volts_per_code[ch]), SCOPE_PLOT_WIDTH,
                             f->peak_min[ch], f->peak_max[ch], f->peak_strength[ch]);
    if (v->density_enabled) {
        ok = density(b, v, first, samples, ch, filtered, &f->density[ch][0][0]);
        memset(f->connections[ch], 0, sizeof(f->connections[ch]));
        if (ok && filtered)
            ok = waveform_view_connect_f64(filtered, count,
                v->zero_y[ch] * rows / SCOPE_PLOT_HEIGHT,
                v->pixels_per_volt[ch] * rows / SCOPE_PLOT_HEIGHT,
                first, samples, SCOPE_PLOT_WIDTH, rows, &f->connections[ch][0][0]);
        else if (ok) {
            double positions[256];
            unsigned code;
            for (code = 0; code < 256; ++code)
                positions[code] = (v->zero_y[ch] - (code - b->zero_code[ch]) *
                    b->volts_per_code[ch] * v->pixels_per_volt[ch]) * rows / SCOPE_PLOT_HEIGHT;
            ok = waveform_view_connect_u8(b->data + ch, count, 2, positions, first, samples,
                                          SCOPE_PLOT_WIDTH, rows, &f->connections[ch][0][0]);
        }
    }
    free(workspace);
    return ok;
}

int capture_process(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f)
{
    unsigned ch;
    CaptureView overlap;
    double display_first, display_samples;
    uint64_t start_ns = scope_clock_ns();
    if (!capture_buffer_valid(b) || !v || !f)
        return 0;
    if(v->density_rows>SCOPE_PLOT_HEIGHT || v->filter_level[0]>3 || v->filter_level[1]>3 ||
       !isfinite(v->noise_rms[0]) || !isfinite(v->noise_rms[1]) || v->noise_rms[0]<0 || v->noise_rms[1]<0)return 0;
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
    memset(f, 0, offsetof(DisplayFrame,connections));
    f->sequence = b->sequence;
    f->capture_id = b->accepted_ns;
    f->calibration_mask=v->calibration_mask;
    f->baseline_mask=v->baseline_mask;
    f->sample_rate_hz = b->sample_rate_hz;
    memcpy(f->volts_per_code,b->volts_per_code,sizeof(f->volts_per_code));
    f->peak_detect = b->peaks!=NULL;
    f->display_samples = display_samples;
    f->fft_size = 1024;
    while (f->fft_size < 1048576 && f->fft_size * 2 <= v->samples) f->fft_size *= 2;
    f->trigger_sample = b->trigger_sample;
    f->trigger_x = b->trigger_sample != CAPTURE_NO_TRIGGER && b->trigger_sample >= b->first_sample ?
        ((double)(b->trigger_sample - b->first_sample) - display_first) * SCOPE_PLOT_WIDTH / display_samples :
        SCOPE_PLOT_WIDTH / 2.0;
    f->view_start = v->start;
    f->view_samples = v->samples;
    for (ch = 0; ch < 2; ++ch) {
        CaptureView corrected_view=*v;double *corrected=NULL;
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
        if(low<=1 || peak>=254)f->clipped_mask|=1U<<ch;
        if (crossings > 1 && last > first)
            f->frequency_hz[ch] = (double)(crossings - 1) * b->sample_rate_hz / (last - first);
        f->duty[ch] = v->samples ? 100.0 * high / v->samples : NAN;
        if(v->response[ch] && v->response[ch]->count>=2){
            size_t count=b->bytes/2,k;
            corrected=malloc(count*sizeof(double));if(!corrected)return 0;
            for(k=0;k<count;++k)corrected[k]=sample_voltage(b,k,ch);
            if(!channel_response_correct(corrected,count,(double)b->sample_rate_hz,v->response[ch])){free(corrected);return 0;}
            corrected_view.corrected[ch]=corrected;
            if(v->samples){
                double sum=0,squares=0,lo=corrected[v->start],hi=lo;
                for(k=v->start;k<v->start+v->samples;++k){double x=corrected[k];sum+=x;squares+=x*x;if(x<lo)lo=x;if(x>hi)hi=x;}
                f->minimum[ch]=lo;f->maximum[ch]=hi;f->mean[ch]=sum/v->samples;f->rms[ch]=sqrt(squares/v->samples);
            }
        }
        if (!v->skip_spectrum && !spectrum(b, &corrected_view, ch, f->fft_size, f->fft[ch],&f->fundamental_hz[ch],&f->fundamental_amplitude[ch])){free(corrected);return 0;}
        if (!render_channel(b, &corrected_view, f, ch, display_first, display_samples)){free(corrected);return 0;}
        free(corrected);
    }
    f->density_valid = v->density_enabled;
    f->density_rows = v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
    f->processing_ns = scope_clock_ns() - start_ns;
    return 1;
}
