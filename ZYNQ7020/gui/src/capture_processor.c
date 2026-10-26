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
    if(!filtered){
        unsigned previous=b->data[ch],lo=previous,hi=previous;
        for(i=1;i<count;++i){
            unsigned value=b->data[2*i+ch];
            if(value<lo)lo=value;if(value>hi)hi=value;
            ++differences[value>previous?value-previous:previous-value];
            previous=value;
        }
        low=lo;high=hi;
    }else for(i=0;i<count;++i){
        double value=filtered[i];
        if(value<low)low=value;if(value>high)high=value;
        if(i){unsigned delta=(unsigned)(fabs(value-filtered[i-1])+.5);++differences[delta>255?255:delta];}
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

typedef struct {
    double level,band,previous,crossing;
    size_t crossing_sample;
    int side;
} EdgeDetector;

static void edge_init(EdgeDetector *e,double level,double band,double value)
{
    *e=(EdgeDetector){level,band,value,0,SIZE_MAX,value<=level-band?-1:value>=level+band?1:0};
}
/* Confirmation and timestamp are separate: ringing inside the guard cannot
   create another edge, and interpolation keeps the original level crossing. */
static int edge_advance(EdgeDetector *e,double value,size_t sample)
{
    int direction=0;
    if(!e->band){
        direction=e->previous<=e->level && value>e->level?1:
            e->previous>=e->level && value<e->level?-1:0;
        if(direction){e->crossing_sample=sample;e->crossing=sample-1+(e->level-e->previous)/(value-e->previous);}
    }else{
        if(e->crossing_sample==SIZE_MAX &&
           ((e->side<0 && e->previous<=e->level && value>e->level) ||
            (e->side>0 && e->previous>=e->level && value<e->level))){
            e->crossing_sample=sample;
            e->crossing=sample-1+(e->level-e->previous)/(value-e->previous);
        }
        int side=value<=e->level-e->band?-1:value>=e->level+e->band?1:0;
        if(side){
            if(e->side && side!=e->side && e->crossing_sample!=SIZE_MAX)direction=side;
            e->side=side;
            if(!direction)e->crossing_sample=SIZE_MAX;
        }
    }
    e->previous=value;
    return direction;
}

static int find_trigger(const CaptureBuffer *b, unsigned ch, unsigned edge,
                         double volts, unsigned hysteresis_codes,
                         uint64_t first_allowed, uint64_t *sample,const double *filtered,size_t *confirmed,size_t *history_start,size_t search_start,int *pending)
{
    size_t i, start = 1, history;
    double level;
    EdgeDetector detector;
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
    history = (search_start>start?search_start:start) - 1;
    while (history && trigger_code(b,ch,history,filtered) > level - hysteresis_codes &&
           trigger_code(b,ch,history,filtered) < level + hysteresis_codes) --history;
    if(history_start)*history_start=history;
    edge_init(&detector,level,hysteresis_codes,trigger_code(b,ch,history,filtered));
    for(i=history+1;i<b->bytes/2;++i){
        int direction=edge_advance(&detector,trigger_code(b,ch,i,filtered),i);
        if(direction){
            size_t crossing=detector.crossing_sample;
            detector.crossing_sample=SIZE_MAX;
            if(crossing>=start && ((direction>0 && edge!=1) || (direction<0 && edge!=0))){
                *sample=b->first_sample+crossing;if(confirmed)*confirmed=i;return 1;
            }
        }
    }
    if(pending)*pending=detector.crossing_sample!=SIZE_MAX && detector.crossing_sample>=start &&
        ((detector.side<0 && edge!=1) || (detector.side>0 && edge!=0));
    return 0;
}
int capture_find_trigger(const CaptureBuffer *b,unsigned ch,unsigned edge,double volts,unsigned hysteresis,
                         uint64_t first_allowed,uint64_t *sample)
{
    return find_trigger(b,ch,edge,volts,hysteresis,first_allowed,sample,NULL,NULL,NULL,0,NULL);
}
int capture_trigger_bounds_possible(const CaptureBuffer *b,const double minimum[2],
    const double maximum[2],unsigned ch,unsigned edge,double volts,unsigned hysteresis)
{
    if(ch>=2 || edge>2 || !isfinite(volts) || !isfinite(b->volts_per_code[ch]) ||
       b->volts_per_code[ch]<=0 || !isfinite(minimum[ch]) || !isfinite(maximum[ch]) || minimum[ch]>maximum[ch])return 1;
    double level=volts/b->volts_per_code[ch]+b->zero_code[ch];
    int low=minimum[ch]<=level-hysteresis,high=maximum[ch]>=level+hysteresis;
    return edge==0?high:edge==1?low:low&&high;
}
/* Both filters are convex: a crossing needs denoised values on both
   sides of its level. Scan codes first and denoise only tiles whose
   raw bounds can reach a still-missing side. Quiet DMA blocks must not pay
   for repeatedly filtering the entire record while newer bursts overwrite it. */
static int trigger_range_possible(const CaptureBuffer *b,unsigned ch,size_t first_sample,double level,unsigned band,double noise,size_t *anchor)
{
    enum { TILE=256 };
    double values[TILE+2*SIGNAL_FILTER_DENOISE_RADIUS];
    size_t count=b->bytes/2;
    size_t radius=signal_filter_padding(0,noise>0);
    unsigned sides=0;
    for(size_t first=first_sample;first<count;first+=TILE){
        size_t end=count-first>TILE?first+TILE:count,i=first;
        unsigned low=255,high=0;
#if defined(__ARM_NEON)
        uint8x16_t lo=vdupq_n_u8(255),hi=vdupq_n_u8(0);
        for(;end-i>=16;i+=16){
            uint8x16_t v=vld2q_u8(b->data+2*i).val[ch];
            lo=vminq_u8(lo,v);hi=vmaxq_u8(hi,v);
        }
        low=minimum16(lo);high=maximum16(hi);
#endif
        for(;i<end;++i){unsigned v=b->data[2*i+ch];if(v<low)low=v;if(v>high)high=v;}
        /* A denoised value also depends on its neighbours. Include
           those in the raw bound, without counting them as tile outputs. */
        size_t begin=first>radius?first-radius:0,limit=count-end>radius?end+radius:count;
        for(i=begin;i<first;++i){unsigned v=b->data[2*i+ch];if(v<low)low=v;if(v>high)high=v;}
        for(i=end;i<limit;++i){unsigned v=b->data[2*i+ch];if(v<low)low=v;if(v>high)high=v;}
        unsigned possible=(low<=level-band?1u:0u)|(high>=level+band?2u:0u);
        if(!(possible&~sides))continue;
        if(noise>0){
            signal_filter_denoise_u8(b->data+begin*2+ch,limit-begin,2,noise,values);
            for(i=first-begin;i<end-begin;++i){
                if(values[i]<=level-band)sides|=1;
                if(values[i]>=level+band)sides|=2;
            }
        }else sides|=possible;
        if(sides==3){*anchor=first;return 1;}
    }
    return 0;
}
int capture_find_trigger_filtered(const CaptureBuffer *b,unsigned ch,unsigned edge,double volts,unsigned hysteresis,
                                  unsigned filter,double noise_rms,uint64_t first_allowed,uint64_t *sample)
{
    double *workspace;size_t count,probe,start,begin,padding,i,anchor;int found;
    if(!isfinite(noise_rms) || noise_rms<0)return 0;
    if(!filter && noise_rms<=0)return capture_find_trigger(b,ch,edge,volts,hysteresis,first_allowed,sample);
    if(!capture_buffer_valid(b) || ch>=2 || edge>2 || !isfinite(volts) || !sample ||
       b->volts_per_code[ch]<=0 || filter>3 || b->bytes/2>SIZE_MAX/(2*sizeof(double)))return 0;
    count=b->bytes/2;
    if(first_allowed>b->first_sample && first_allowed-b->first_sample>=count)return 0;
    start=first_allowed>b->first_sample?(size_t)(first_allowed-b->first_sample):1;
    padding=signal_filter_padding(filter,noise_rms>0);
    double level=volts/b->volts_per_code[ch]+b->zero_code[ch],noise=noise_rms/b->volts_per_code[ch];
    if(!trigger_range_possible(b,ch,0,level,hysteresis,noise,&anchor))return 0;
    if(start>1 && !trigger_range_possible(b,ch,start>padding+1?start-padding-1:0,level,0,noise,&anchor))return 0;
    /* Before this tile one required side was absent. A centred filter can reach
       into it by padding samples; recover any older pending crossing below. */
    if(anchor>padding && start<anchor-padding)start=anchor-padding;
    begin=start>padding+256?start-padding-256:0;
    probe=start+padding*2+8192;
    if(probe>count)probe=count;
    for(;;){
        CaptureBuffer window=*b;
        size_t confirmed=0,history=0,length=probe-begin;int pending=0;
        window.data+=begin*2;window.bytes=length*2;window.capacity-=begin*2;
        window.first_sample+=begin;window.peaks=NULL;window.trigger_sample=CAPTURE_NO_TRIGGER;
        workspace=malloc(length*2*sizeof(double));if(!workspace)return 0;
        double *input=workspace,*output=workspace+length;
        if(noise_rms>0)signal_filter_denoise_u8(window.data+ch,length,2,noise_rms/b->volts_per_code[ch],input);
        else for(i=0;i<length;++i)input[i]=window.data[2*i+ch];
        if(filter){signal_filter_apply(input,length,filter,output);input=output;}
        /* Truncated outputs at a temporary right boundary must not confirm
           or cancel a pending edge before the next window is examined. */
        if(probe<count)window.bytes-=padding*2;
        found=find_trigger(&window,ch,edge,volts,hysteresis,first_allowed,sample,input,&confirmed,&history,start-begin,&pending);
        free(workspace);
        /* Recover the source side through an arbitrarily long stay inside the
           hysteresis band. Both filter boundaries need their complete support. */
        if(begin && history<padding){
            size_t context=start-begin;
            begin=context>begin?0:begin-context;
            continue;
        }
        if(probe==count || found)return found;
        if(!found && !pending){
            /* No unfinished crossing needs this prefix. Bounds include the
               overlap at the next valid filter output, so a boundary edge
               still brackets the level. Skip directly to the next candidate. */
            size_t next=probe>padding+1?probe-padding-1:0;
            if(!trigger_range_possible(b,ch,next,level,0,noise,&anchor))return 0;
            start=anchor>padding?anchor-padding:1;
            if(start<next)start=next;
            begin=start>padding+256?start-padding-256:0;
            probe=start+padding*2+8192;if(probe>count)probe=count;
            continue;
        }
        size_t ahead=probe-start;
        probe=ahead>(count-start)/2?count:start+ahead*2;
    }
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
    size_t x,i;
    unsigned peak=0,code;
    size_t used=0,offsets[SCOPE_PLOT_WIDTH+1];
    size_t capacity=b->bytes/2<SCOPE_PLOT_WIDTH*SCOPE_PLOT_HEIGHT?b->bytes/2:SCOPE_PLOT_WIDTH*SCOPE_PLOT_HEIGHT;
    uint32_t *hits=malloc(capacity*sizeof(*hits));
    if(!hits)return 0;
    unsigned height=v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
    int rows[256];
    uint16_t counts[SCOPE_PLOT_HEIGHT];
    uint8_t palette[UINT16_MAX+1];
    for(code=0;code<256;++code){
        double y=(v->zero_y[ch]-(code-b->zero_code[ch])*b->volts_per_code[ch]*v->pixels_per_volt[ch])*height/SCOPE_PLOT_HEIGHT;
        rows[code]=y>=0 && y<height?(int)y:-1;
    }
    memset(levels,0,SCOPE_PLOT_WIDTH*SCOPE_PLOT_HEIGHT);
    /* Keep each column histogram in L1 and store only occupied pixels. Counts
       retain 16 bits; the global brightness scale is applied after all columns. */
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        unsigned low=height,high=0;
        size_t a=(size_t)ceil(fmax(0,fmin(first+x*samples/SCOPE_PLOT_WIDTH,b->bytes/2)));
        size_t end=(size_t)ceil(fmax(0,fmin(first+(x+1)*samples/SCOPE_PLOT_WIDTH,b->bytes/2)));
        offsets[x]=used;
        memset(counts,0,height*sizeof(*counts));
        for(i=a;i<end;++i){
            int y;
            if(filtered){
                double position=(v->zero_y[ch]-filtered[i]*v->pixels_per_volt[ch])*height/SCOPE_PLOT_HEIGHT;
                y=position>=0 && position<height?(int)position:-1;
            }else y=rows[b->data[2*i+ch]];
            if(y>=0){
                if(counts[y]<UINT16_MAX)++counts[y];
                if(counts[y]>peak)peak=counts[y];
                if((unsigned)y<low)low=(unsigned)y;
                if((unsigned)y>high)high=(unsigned)y;
            }
        }
        for(code=low;code<=high && code<height;++code)if(counts[code])
            hits[used++]=((uint32_t)counts[code]<<16)|code;
    }
    offsets[SCOPE_PLOT_WIDTH]=used;
    for(code=0;code<=peak;++code)palette[code]=(uint8_t)waveform_density_level(code,peak);
    for(x=0;x<SCOPE_PLOT_WIDTH;++x)for(i=offsets[x];i<offsets[x+1];++i)
        levels[x*SCOPE_PLOT_HEIGHT+(hits[i]&65535)]=palette[hits[i]>>16];
    free(hits);
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
    /* Retain unresolved signal excursions, suppress only the measured noise
       envelope. Decide per native interval so aggregation cannot restore noise
       just because one screen column or measurement also contains a real edge. */
    *low=INFINITY;*high=-INFINITY;
    for(size_t i=first;i<end;++i){
        double a,b;
        source_envelope(source->buffer,i,i+1,channel,&a,&b);
        if(b-a<=8*source->noise){
            size_t last=(i+1)*2,count=source->buffer->bytes/2;
            if(last>count)last=count;
            filtered_range(source->values,i*2,last,channel,&a,&b);
        }
        if(a<*low)*low=a;if(b>*high)*high=b;
    }
}
static int compare_periods(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;
    return x<y?-1:x>y;
}

static void measure_timing(const CaptureBuffer *b,unsigned ch,const double *values,
                            size_t first,size_t end,double minimum,double maximum,
                            double *frequency,double *duty)
{
    unsigned histogram[256]={0};size_t count=end-first,total=0,n=0;
    double range,low,high,histogram_scale;
    if(count && !values && b->peaks)source_range(b,first,end,ch,&minimum,&maximum);
    range=maximum-minimum;low=minimum;high=maximum;
    double last_rise=-1,last_fall=-1,all_span=0,all_width=0,all_squares=0;
    size_t capacity=count/2<4096?count/2:4096;
    *frequency=0;*duty=NAN;
    if(count<3 || !isfinite(range) || range<4*fabs(b->volts_per_code[ch]) || range<=0)return;
    histogram_scale=255/range;
    for(size_t i=first;i<end;++i){
        double value=values?values[i]:sample_voltage(b,i,ch);
        double bin=(value-minimum)*histogram_scale;
        unsigned code=bin<0?0:bin>255?255:(unsigned)bin;
        ++histogram[code];
    }
    for(unsigned code=0;code<256;++code){
        total+=histogram[code];
        if(total>count/20){low=minimum+code*range/255;break;}
    }
    total=0;
    for(unsigned code=0;code<256;++code){
        total+=histogram[code];
        if(total>=count-count/20){high=minimum+code*range/255;break;}
    }
    /* Narrow pulses can occupy less than a percentile bucket. Keep their full
       excursion rather than treating a small duty cycle as a DC input. */
    if(high-low<range*.2){low=minimum;high=maximum;}
    double level=(low+high)*.5,band=fmax(2*fabs(b->volts_per_code[ch]),(high-low)*.15);
    double *periods=malloc(capacity*3*sizeof(*periods));
    if(!periods)return;
    double *widths=periods+capacity,*ordered=widths+capacity;
    EdgeDetector detector;
    edge_init(&detector,level,band,values?values[first]:sample_voltage(b,first,ch));
    for(size_t i=first+1;i<end;++i){
        int edge=edge_advance(&detector,values?values[i]:sample_voltage(b,i,ch),i);
        if(edge){
            double at=detector.crossing;detector.crossing_sample=SIZE_MAX;
            if(edge<0)last_fall=at;
            else{
                if(last_rise>=0 && last_fall>last_rise && last_fall<at){
                    double period=at-last_rise,width=last_fall-last_rise;
                    all_span+=period;all_width+=width;all_squares+=period*period;
                    if(n<capacity){periods[n]=ordered[n]=period;widths[n]=width;}
                    ++n;
                }
                last_rise=at;
            }
        }
    }
    if(n>capacity){
        double mean=all_span/n,variance=all_squares/n-mean*mean;
        if(variance<=mean*mean*.04){
            *frequency=n*b->sample_rate_hz/all_span;*duty=100*all_width/all_span;
        }
    }else if(n){
        qsort(ordered,n,sizeof(*ordered),compare_periods);
        double median=(ordered[(n-1)/2]+ordered[n/2])*.5,span=0,positive=0;
        size_t accepted=0;
        for(size_t i=0;i<n;++i)if(fabs(periods[i]-median)<=median*.2){
            span+=periods[i];positive+=widths[i];++accepted;
        }
        /* Irregular noise must not become a plausible numerical frequency. */
        if(accepted && accepted>=n-n/4){
            *frequency=accepted*b->sample_rate_hz/span;*duty=100*positive/span;
        }
    }
    free(periods);
}

static int render_channel(const CaptureBuffer *b, const CaptureView *v, DisplayFrame *f,
                           unsigned ch, double first, double samples)
{
    WaveformSource source = {0};
    size_t count = b->bytes / 2, i;
    double *workspace = NULL;
    AutoEnvelope auto_source={0};
    const double *filtered = v->corrected[ch];
    CaptureBuffer window;
    int ok = 1;
    unsigned rows = v->density_rows ? v->density_rows : SCOPE_PLOT_HEIGHT;
    if(v->filter_level[ch] || v->noise_rms[ch]>0){
        /* Only the displayed interval and its filter support are needed.
           Two extra neighbours cover interpolation and Peak Detect pairs. */
        size_t padding=signal_filter_padding(v->filter_level[ch],v->noise_rms[ch]>0)+2;
        size_t begin=(size_t)floor(fmax(0,fmin(first,count)));
        size_t end=(size_t)ceil(fmax(0,fmin(first+samples,count)));
        begin=begin>padding?begin-padding:0;
        end=count-end>padding?end+padding:count;
        if(b->peaks){begin&=~(size_t)1;end=(end+1)&~(size_t)1;}
        window=*b;window.data+=begin*2;window.bytes=(end-begin)*2;
        window.capacity-=begin*2;window.first_sample+=begin;
        window.trigger_sample=CAPTURE_NO_TRIGGER;
        if(window.peaks)window.peaks+=begin*2;
        b=&window;count=end-begin;first-=begin;
        if(filtered)filtered+=begin;
    }
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
        double *input=workspace,*output=workspace+count,*swap;
        if(v->noise_rms[ch]>0 && !filtered){
            signal_filter_denoise_u8(b->data+ch,count,2,v->noise_rms[ch]/fabs(b->volts_per_code[ch]),input);
            for(i=0;i<count;++i)input[i]=(input[i]-b->zero_code[ch])*b->volts_per_code[ch];
        }else{
            for (i = 0; i < count; ++i) input[i] = filtered?filtered[i]:sample_voltage(b, i, ch);
            if(v->noise_rms[ch]>0){signal_filter_denoise(input,count,v->noise_rms[ch],output);swap=input;input=output;output=swap;}
        }
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
    size_t visible_first=(size_t)floor(fmax(0,fmin(first,count)));
    size_t visible_end=(size_t)ceil(fmax(0,fmin(first+samples,count)));
    double timing_low=f->minimum[ch],timing_high=f->maximum[ch];
    if(filtered && visible_end>visible_first){
        double sum=0,squares=0,low=filtered[visible_first],high=low;
        for(i=visible_first;i<visible_end;++i){
            double value=filtered[i];
            sum+=value;squares+=value*value;
            if(value<low)low=value;if(value>high)high=value;
        }
        f->mean[ch]=sum/(visible_end-visible_first);
        f->rms[ch]=sqrt(squares/(visible_end-visible_first));
        timing_low=low;timing_high=high;
        /* Only extrema represented by this source may extend its range.
           Manual filtering removes native buckets with unknown peak times. */
        if(source.envelope){
            double envelope_low,envelope_high;
            source.envelope(source.context,visible_first/source.envelope_samples,
                (visible_end+source.envelope_samples-1)/source.envelope_samples,ch,
                &envelope_low,&envelope_high);
            if(envelope_low<low)low=envelope_low;
            if(envelope_high>high)high=envelope_high;
        }
        f->minimum[ch]=low;f->maximum[ch]=high;
    }
    measure_timing(b,ch,filtered,visible_first,visible_end,timing_low,timing_high,
        &f->frequency_hz[ch],&f->duty[ch]);
    waveform_view_reduce(&source, first, samples, ch, v->zero_y[ch], v->pixels_per_volt[ch],
                         SCOPE_PLOT_WIDTH, f->y[ch], f->y_min[ch], f->y_max[ch]);
    waveform_view_peak_bounds(&source, first, samples, ch, v->zero_y[ch], v->pixels_per_volt[ch],
                             fabs(b->volts_per_code[ch]), SCOPE_PLOT_WIDTH,
                             f->peak_min[ch], f->peak_max[ch], f->peak_strength[ch]);
    if (v->density_enabled) {
        ok = density(b, v, first, samples, ch, filtered, &f->density[ch][0][0]);
        if(rows<SCOPE_PLOT_HEIGHT)
            memset(&f->connections[ch][0][0]+SCOPE_PLOT_WIDTH*rows,0,
                SCOPE_PLOT_WIDTH*(SCOPE_PLOT_HEIGHT-rows));
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
    if(v->density_rows>SCOPE_PLOT_HEIGHT || (v->hidden_channel_mask&~3u) || v->filter_level[0]>3 || v->filter_level[1]>3 ||
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
        size_t i;
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
        f->raw_mean[ch]=f->mean[ch];f->raw_rms[ch]=f->rms[ch];
        if(low<=1 || peak>=254)f->clipped_mask|=1U<<ch;
        f->duty[ch]=NAN;
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
        if(v->hidden_channel_mask&(1u<<ch)){
            for(size_t x=0;x<SCOPE_PLOT_WIDTH;++x){
                f->y[ch][x]=f->y_min[ch][x]=f->y_max[ch][x]=WAVEFORM_NO_SAMPLE;
                f->peak_min[ch][x]=f->peak_max[ch][x]=WAVEFORM_NO_SAMPLE;
            }
            memset(f->connections[ch],0,sizeof(f->connections[ch]));
            memset(f->density[ch],0,sizeof(f->density[ch]));
        }else if(!render_channel(b,&corrected_view,f,ch,display_first,display_samples)){free(corrected);return 0;}
        free(corrected);
    }
    f->density_valid = v->density_enabled;
    f->density_rows = v->density_rows?v->density_rows:SCOPE_PLOT_HEIGHT;
    f->processing_ns = scope_clock_ns() - start_ns;
    return 1;
}
