#include "capture_pipeline.h"
#include "capture_file.h"
#include "capture_history.h"
#include "signal_filter.h"
#include "capture_settings.h"
#ifdef SCOPE_ENABLE_ZYNQ
#include "capture_zynq.h"
#include "analysis_zynq.h"
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct CapturePipeline {
    CaptureRing ring;
    ScopeMutex mutex;
    ScopeThread acquisition, processing;
    CaptureSource source;
    DemoCaptureSource demo;
    FileCaptureSource file_source;
    FILE *file;
    uint8_t *storage[CAPTURE_RING_MAX];
    CaptureRequest request;
    DisplayFrame frames[3];
    unsigned write_slot, ready_slot, reading_slot;
    unsigned frame_pins[3];
    unsigned revision, arm_revision;
    unsigned source_revision, record_source[CAPTURE_RING_MAX];
    int stop, source_error;
    uint64_t bytes, display_replaced, processing_ns, waveforms;
    uint64_t adc_sample_ns;
    uint64_t presented_id,acquired_id;
    int presentation_sync;
    int hardware;
#ifdef SCOPE_ENABLE_ZYNQ
    ZynqCaptureSource zynq;
    ZynqAnalysis analysis;
    CaptureMetrics hardware_metrics;
#endif
};
static int stopped(CapturePipeline *p)
{
    int result;
    scope_mutex_lock(&p->mutex);
    result = p->stop;
    scope_mutex_unlock(&p->mutex);
    return result;
}
static void acquisition_worker(void *context)
{
    CapturePipeline *p = context;
    while (!stopped(p)) {
        int slot, run, result;
        scope_mutex_lock(&p->mutex);
        run = p->request.running;
        scope_mutex_unlock(&p->mutex);
        if (!run) {
            scope_sleep_ms(10);
            continue;
        }
        slot = capture_ring_begin(&p->ring);
        if (slot < 0) {
            scope_sleep_ms(5);
            continue;
        }
        result = p->source.read(&p->source, &p->ring.buffer[slot]);
        if (result != 1) {
            capture_ring_cancel(&p->ring, slot);
            scope_mutex_lock(&p->mutex);
            if (result < 0)
                p->source_error = 1;
            p->request.running = 0;
            scope_mutex_unlock(&p->mutex);
            continue;
        }
        scope_mutex_lock(&p->mutex);
        if (!p->request.running) {
            scope_mutex_unlock(&p->mutex);
            capture_ring_cancel(&p->ring, slot);
            continue;
        }
        p->bytes += p->ring.buffer[slot].bytes;
        scope_mutex_unlock(&p->mutex);
        if (!capture_ring_publish(&p->ring, slot))
            capture_ring_cancel(&p->ring, slot);
        /* Demo cadence only; hardware waits for DMA completion in its adapter. */
        scope_sleep_ms(20);
    }
}
/* Keep the acquired codes and their nominal zero immutable. Apply the current
   zero to a metadata view, including when rerendering a retained STOP record. */
static const ChannelBaseline *capture_baseline(const CaptureRequest *request,const CaptureBuffer *b,unsigned ch)
{
    for(unsigned i=0;i<INPUT_RANGE_STAGE_COUNT;++i){
        unsigned stage=request->debug.enabled?request->range_stage[ch]:i;
        const ChannelBaseline *cal=&request->baseline[ch][stage];
        double gain=b->volts_per_code[ch];
        if(((cal->valid&CHANNEL_ZERO_VALID) && fabs(cal->volts_per_code-gain)<=gain*1e-6) ||
           ((cal->valid&CHANNEL_NOISE_VALID) && fabs(cal->noise_volts_per_code-gain)<=gain*1e-6))return cal;
        if(request->debug.enabled)break;
    }
    return NULL;
}
static CaptureBuffer calibrated_buffer(const CaptureBuffer *b,const CaptureRequest *request)
{
    CaptureBuffer result=*b;
    for(unsigned ch=0;ch<2;++ch)if(!(request->baseline_mask&(1U<<ch))){
        double zero=channel_baseline_zero(capture_baseline(request,b,ch),b->volts_per_code[ch]);
        if(zero)result.zero_code[ch]+=zero/b->volts_per_code[ch];
    }
    return result;
}

static int process_frame(CapturePipeline *p, const CaptureBuffer *raw,
                           const CaptureRequest *request, uint64_t preprocessing_ns)
{
    CaptureBuffer calibrated=calibrated_buffer(raw,request);
    const CaptureBuffer *b=&calibrated;
    uint64_t started = scope_clock_ns();
    CaptureView view;
    DisplayFrame *frame;
    double samples = request->duration_seconds * b->sample_rate_hz;
    double base_samples = fmax(request->overview_duration_seconds, request->duration_seconds) * b->sample_rate_hz;
    double base_first;
    /* A live sweep is published as a whole interval, including after a scale
       change that can no longer be rendered from the retained record. */
    if (p->hardware && request->running && !request->roll && b->bytes / 2 < ceil(base_samples))
        return 0;
    /* A slow painter may retain older maps. Skip a presentation rather than
       overwriting a leased frame or making the UI wait for channel processing. */
    scope_mutex_lock(&p->mutex);
    for(p->write_slot=0;p->write_slot<3;++p->write_slot)
        if(p->write_slot+1!=p->reading_slot && p->write_slot+1!=p->ready_slot && !p->frame_pins[p->write_slot])break;
    if(p->write_slot==3 && p->ready_slot && p->ready_slot!=p->reading_slot && !p->frame_pins[p->ready_slot-1]){
        p->write_slot=p->ready_slot-1;p->ready_slot=0;++p->display_replaced;
    }
    scope_mutex_unlock(&p->mutex);
    if(p->write_slot==3)return 0;
    frame=&p->frames[p->write_slot];
    memset(&view, 0, sizeof(view));
#ifdef SCOPE_ENABLE_ZYNQ
    if(p->hardware && p->analysis.fd>=0)view.analyzer=&p->analysis.analyzer;
#endif
    view.display_samples = samples < 1 ? 1 : samples;
    base_first = ((double)(b->bytes / 2) - base_samples) * request->pan_fraction;
    if (b->trigger_sample != CAPTURE_NO_TRIGGER && b->trigger_sample >= b->first_sample)
        base_first = (double)(b->trigger_sample - b->first_sample) -
                     base_samples * (1.0 - request->pan_fraction);
    if (request->roll) base_first = (double)(b->bytes / 2) - base_samples +
        (request->running?0:(request->pan_fraction - 0.5) * base_samples);
    view.display_first = base_first + (base_samples - view.display_samples) * 0.5 +
                         request->zoom_offset_seconds * b->sample_rate_hz;
    if(request->roll && view.display_samples<=b->bytes/2)
        view.display_first=fmax(0,fmin(view.display_first,b->bytes/2-view.display_samples));
    view.skip_spectrum = !request->spectrum_enabled;
    view.calibration_mask=request->calibration_mask;
    view.baseline_mask=request->baseline_mask;
    if(!request->baseline_mask && !request->calibration_mask)for(unsigned ch=0;ch<2;++ch)
        view.noise_rms[ch]=channel_baseline_noise(capture_baseline(request,raw,ch),raw->volts_per_code[ch]);
    view.density_enabled = request->density_enabled;
    view.density_rows = request->density_rows;
    view.hidden_channel_mask = request->hidden_channel_mask;
    memcpy(view.filter_level, request->filter_level, sizeof(view.filter_level));
    for(unsigned channel=0;channel<2;++channel)
        if(request->response_enabled[channel])view.response[channel]=&request->response[channel];
    memcpy(view.zero_y, request->zero_y, sizeof(view.zero_y));
    memcpy(view.pixels_per_volt, request->pixels_per_volt, sizeof(view.pixels_per_volt));
    if (capture_process(b, &view, frame)) {
        if (request->overview_duration_seconds > request->duration_seconds) {
            CaptureView base = view;
            DisplayFrame *overview = malloc(sizeof(*overview));
            base.display_samples = request->overview_duration_seconds * b->sample_rate_hz;
            base.display_first = base_first;
            base.skip_spectrum = 1;
            base.density_enabled = 0;
            if (overview && capture_process(b, &base, overview))
                memcpy(frame->overview, overview->y, sizeof(frame->overview));
            free(overview);
            frame->overview_start = (int)lround((view.display_first - base_first) * SCOPE_PLOT_WIDTH / base_samples);
            frame->overview_end = (int)lround((view.display_first + view.display_samples - base_first) * SCOPE_PLOT_WIDTH / base_samples);
        } else memcpy(frame->overview, frame->y, sizeof(frame->overview));
        if (b->trigger_sample == CAPTURE_NO_TRIGGER)
            frame->trigger_x = ((1.0 - request->pan_fraction) * base_samples -
                (base_samples - view.display_samples) * 0.5 - request->zoom_offset_seconds * b->sample_rate_hz) *
                SCOPE_PLOT_WIDTH / view.display_samples;
        frame->processing_ns = scope_clock_ns() - started + preprocessing_ns;
        scope_mutex_lock(&p->mutex);
        if (p->ready_slot) ++p->display_replaced;
        p->ready_slot = p->write_slot + 1;
        ++p->waveforms;
        p->processing_ns = frame->processing_ns;
        scope_mutex_unlock(&p->mutex);
        return 1;
    }
#ifdef SCOPE_ENABLE_ZYNQ
    if(p->hardware && p->analysis.failed){
        scope_mutex_lock(&p->mutex);p->source_error=1;p->request.running=0;scope_mutex_unlock(&p->mutex);
    }
#endif
    return 0;
}
static void processing_worker(void *context)
{
    CapturePipeline *p = context;
    int retained = -1;
    unsigned processed_revision = UINT32_MAX;
    while (!stopped(p)) {
        /* Preserve the first completed frame until the consumer takes it.
           A second raw record can be processed in parallel with its painting. */
        scope_mutex_lock(&p->mutex);
        int blocked=p->presentation_sync && p->ready_slot;
        scope_mutex_unlock(&p->mutex);
        if(blocked){scope_sleep_ms(1);continue;}
        int slot = capture_ring_take(&p->ring, 20);
        CaptureRequest request;
        unsigned revision,source_revision;
        if (slot >= 0) {
            if (retained >= 0)
                capture_ring_release(&p->ring, retained);
            retained = slot;
        }
        scope_mutex_lock(&p->mutex);
        request = p->request;
        revision = p->revision;
        source_revision = p->source_revision;
        scope_mutex_unlock(&p->mutex);
        if(p->hardware && retained>=0 && request.running &&
           p->record_source[retained]!=source_revision)continue;
        if (retained >= 0 && (slot >= 0 || revision != processed_revision)) {
            const CaptureBuffer *b=&p->ring.buffer[retained];
            uint64_t preprocessing=p->hardware && b->accepted_ns>=b->software_ns?b->accepted_ns-b->software_ns:0;
            if(process_frame(p,b,&request,preprocessing))processed_revision = revision;
        }
    }
    if (retained >= 0)
        capture_ring_release(&p->ring, retained);
}
#ifdef SCOPE_ENABLE_ZYNQ
/* One complete incoming payload plus the selected record must fit together:
   otherwise a rare edge can be overwritten before its posttrigger arrives. */
enum { HARDWARE_HISTORY_SAMPLES = CAPTURE_MAX_SAMPLES + (8u << 17) };
static void retain_record(CaptureBuffer *b, unsigned depth, double pan,size_t minimum_pre,size_t minimum_post)
{
    size_t count=b->bytes/2,first;
    if(count<=depth)return;
    first=count-depth;
    if(b->trigger_sample!=CAPTURE_NO_TRIGGER){
        size_t trigger=(size_t)(b->trigger_sample-b->first_sample);
        size_t pre=(size_t)ceil(depth*(1.0-pan));
        if(pre>=depth)pre=depth-1;
        if(minimum_pre+minimum_post<=depth){
            if(pre<minimum_pre)pre=minimum_pre;
            if(pre>depth-minimum_post)pre=depth-minimum_post;
        }
        first=trigger>pre?trigger-pre:0;
        if(first>count-depth)first=count-depth;
    }
    if(b->peaks){
        first&=~(size_t)1;
        if(b->trigger_sample!=CAPTURE_NO_TRIGGER && b->trigger_sample-b->first_sample-first>=depth)first+=2;
    }
    b->data+=first*2;b->capacity-=first*2;
    if(b->peaks)b->peaks+=first*2;
    b->first_sample+=first;b->bytes=depth*2;
}
typedef struct {
    ZynqCapturePacket pending[2],context;
    ZynqCapturePacket encoded_context[5];
    unsigned encoded_count;
    unsigned pending_count;
    int have_context,post;
} HardwarePackets;
static int release_packets(CapturePipeline *p,HardwarePackets *q)
{
    int ok=1;
    for(unsigned i=0;i<q->pending_count;++i)ok&=zynq_capture_release_index(&p->zynq,q->pending[i].index);
    for(unsigned i=0;i<q->encoded_count;++i)ok&=zynq_capture_release_index(&p->zynq,q->encoded_context[i].index);
    if(q->have_context)ok&=zynq_capture_release_index(&p->zynq,q->context.index);
    memset(q,0,sizeof(*q));return ok;
}
static int keep_encoded_context(CapturePipeline *p,HardwarePackets *q,const ZynqCapturePacket *packet,unsigned limit)
{
    int ok=1;
    if(q->encoded_count && q->encoded_context[q->encoded_count-1].metadata.sequence+1!=packet->metadata.sequence){
        for(unsigned i=0;i<q->encoded_count;++i)ok&=zynq_capture_release_index(&p->zynq,q->encoded_context[i].index);
        q->encoded_count=0;
    }
    if(q->encoded_count==limit){
        ok&=zynq_capture_release_index(&p->zynq,q->encoded_context[0].index);
        memmove(q->encoded_context,q->encoded_context+1,(q->encoded_count-1)*sizeof(q->encoded_context[0]));--q->encoded_count;
    }
    q->encoded_context[q->encoded_count++]=*packet;return ok;
}
static int decode_history(CapturePipeline *p,CaptureHistory *h,const ZynqCapturePacket *packet,
    const CaptureRequest *request,size_t tail,CaptureBuffer *b)
{
    CaptureBuffer metadata=packet->metadata;
    size_t samples=metadata.bytes/2,first=tail && tail<samples?samples-tail:0;
    if(packet->peak_detect)first&=~(size_t)1;
    samples-=first;metadata.first_sample+=first;metadata.bytes=samples*2;
    uint8_t *data=NULL,*peaks=NULL;
    if(!request->debug.enabled)capture_history_prepare(h,&metadata,packet->peak_detect,&data,&peaks);
    if(!zynq_capture_decode_packet(&p->zynq,packet,first,samples,b,data,peaks,samples*2))return 0;
    for(unsigned ch=0;ch<2;++ch)if(isfinite(request->adc_volts_per_code[ch]) && request->adc_volts_per_code[ch]>0)
        b->volts_per_code[ch]=request->adc_volts_per_code[ch];
    return 1;
}
static size_t retained_samples(const HardwarePackets *packets,const ZynqCapturePacket *packet,const CaptureHistory *history)
{
    size_t samples=packet->metadata.bytes/2;
    const ZynqCapturePacket *next=packet;
    for(unsigned i=packets->encoded_count;i>0;--i){
        const ZynqCapturePacket *previous=&packets->encoded_context[i-1];
        if(previous->metadata.sequence+1!=next->metadata.sequence ||
           previous->metadata.first_sample+previous->metadata.bytes/2!=next->metadata.first_sample ||
           previous->metadata.sample_rate_hz!=next->metadata.sample_rate_hz ||
           previous->peak_detect!=next->peak_detect)break;
        samples+=previous->metadata.bytes/2;next=previous;
    }
    if(history->count && history->latest.sequence+1==next->metadata.sequence &&
       history->latest.first_sample+history->latest.bytes/2==next->metadata.first_sample &&
       history->latest.sample_rate_hz==next->metadata.sample_rate_hz &&
       !!history->latest.peaks==!!next->peak_detect)samples+=history->count;
    return samples;
}
static void count_hardware_block(CapturePipeline *p,const CaptureBuffer *metadata,
    const uint8_t last[2],uint64_t started,uint64_t *expected)
{
    scope_mutex_lock(&p->mutex);
    ++p->hardware_metrics.blocks;
    p->hardware_metrics.adc_code[0]=last[0];p->hardware_metrics.adc_code[1]=last[1];p->adc_sample_ns=started;
    if(metadata->sequence>*expected)p->hardware_metrics.dropped+=metadata->sequence-*expected;
    p->bytes+=metadata->bytes;
    scope_mutex_unlock(&p->mutex);
    *expected=metadata->sequence+1;
}
static void packet_trigger_bounds(const ZynqCapturePacket *summary,const CaptureBuffer *calibrated,
    const CaptureRequest *request,double minimum[2],double maximum[2])
{
    for(unsigned ch=0;ch<2;++ch){
        minimum[ch]=summary->minimum[ch];maximum[ch]=summary->maximum[ch];
        if(summary->window_bounds){
            double noise=channel_baseline_noise(capture_baseline(request,calibrated,ch),calibrated->volts_per_code[ch]);
            double low,high;
            signal_filter_denoise_bounds(minimum[ch],maximum[ch],
                summary->minimum_sum[ch]/17.0,summary->maximum_sum[ch]/17.0,
                summary->maximum_variance[ch]/289.0,noise/calibrated->volts_per_code[ch],&low,&high);
            if(noise<=0 && request->filter_level[ch]==2){low=summary->minimum_sum[ch]/17.0;high=summary->maximum_sum[ch]/17.0;}
            minimum[ch]=fmax(0,low-1e-9);maximum[ch]=fmin(255,high+1e-9);
        }
    }
}
static unsigned packet_trigger_sides(const ZynqCapturePacket *summary,const CaptureBuffer *calibrated,
    const CaptureRequest *request,uint32_t probe_control)
{
    if(summary->probe_valid && summary->probe_control==probe_control)return summary->probe_sides;
    double minimum[2],maximum[2];
    packet_trigger_bounds(summary,calibrated,request,minimum,maximum);
    return (capture_trigger_bounds_possible(calibrated,minimum,maximum,request->trigger_channel,1,request->trigger_voltage,2)?1u:0u)|
        (capture_trigger_bounds_possible(calibrated,minimum,maximum,request->trigger_channel,0,request->trigger_voltage,2)?2u:0u);
}
static uint32_t requested_probe(const CapturePipeline *p,const CaptureRequest *request)
{
    if(p->zynq.probe_supported<0 || request->roll || request->debug.enabled || request->baseline_mask || request->calibration_mask)return 0;
    CaptureBuffer metadata={0};
    memcpy(metadata.volts_per_code,p->zynq.volts_per_code,sizeof(metadata.volts_per_code));
    memcpy(metadata.zero_code,p->zynq.zero_code,sizeof(metadata.zero_code));
    for(unsigned ch=0;ch<2;++ch)if(isfinite(request->adc_volts_per_code[ch]) && request->adc_volts_per_code[ch]>0)
        metadata.volts_per_code[ch]=request->adc_volts_per_code[ch];
    metadata=calibrated_buffer(&metadata,request);
    unsigned ch=(unsigned)request->trigger_channel;
    double gain=metadata.volts_per_code[ch],level=request->trigger_voltage/gain+metadata.zero_code[ch];
    if(!(level>=0 && level<256))return 0;
    double noise=channel_baseline_noise(capture_baseline(request,&metadata,ch),gain)/gain;
    unsigned code=noise>0?capture_probe_power_code(noise*noise):request->filter_level[ch]==2?255:0;
    if(code>255)return 0;
    return OSC_CAPTURE_PROBE_ENABLE|(ch<<16)|(code<<8)|(unsigned)level;
}
static struct osc_capture_dsp requested_dsp(const CapturePipeline *p,const CaptureRequest *request,unsigned rate)
{
    struct osc_capture_dsp config={0};
    if(p->zynq.dsp_supported<0 || request->roll || request->debug.enabled || request->baseline_mask || request->calibration_mask)return config;
    CaptureBuffer metadata={0};
    memcpy(metadata.volts_per_code,p->zynq.volts_per_code,sizeof(metadata.volts_per_code));
    memcpy(metadata.zero_code,p->zynq.zero_code,sizeof(metadata.zero_code));
    for(unsigned ch=0;ch<2;++ch)if(request->adc_volts_per_code[ch]>0)metadata.volts_per_code[ch]=request->adc_volts_per_code[ch];
    metadata=calibrated_buffer(&metadata,request);
    unsigned ch=(unsigned)request->trigger_channel;
    double gain=metadata.volts_per_code[ch],level=request->trigger_voltage/gain+metadata.zero_code[ch];
    double noise=channel_baseline_noise(capture_baseline(request,&metadata,ch),gain)/gain;
    if(!isfinite(level) || level<-32768 || level>32767)return config;
    double power=noise*noise*65536;
    config.control=OSC_CAPTURE_DSP_ENABLE|(ch<<OSC_CAPTURE_DSP_CHANNEL_SHIFT)|
        ((unsigned)request->trigger_edge<<OSC_CAPTURE_DSP_EDGE_SHIFT)|
        (request->filter_level[ch]<<OSC_CAPTURE_DSP_FILTER_SHIFT);
    config.noise_power=power>=UINT32_MAX?UINT32_MAX:(uint32_t)llround(power);
    config.level=(int32_t)llround(level*65536);config.hysteresis=2u<<16;
    config.minimum_sample=(uint64_t)ceil(fmax(request->duration_seconds,request->overview_duration_seconds)*rate*
        (1-request->pan_fraction))+signal_filter_padding(request->filter_level[ch],noise>0)+8;
    config.holdoff_samples=request->holdoff_samples*rate/200000000;
    return config;
}
static int same_dsp(const struct osc_capture_dsp *a,const struct osc_capture_dsp *b)
{
    return a->control==b->control && a->noise_power==b->noise_power && a->level==b->level &&
        a->hysteresis==b->hysteresis && a->holdoff_samples==b->holdoff_samples;
}
static void hardware_worker(void *context)
{
    CapturePipeline *p = context;
    CaptureHistory history;
    HardwarePackets packets={0};
    CaptureBuffer debug = {0}, candidate = {0};
    CaptureDebug previous_debug = {0};
    double previous_gain[2] = {0};
    uint64_t expected = 0, next_trigger = 0, forced = 0, last_display = 0;
    uint64_t pending_presentation[2]={0};
    unsigned presentation_count=0;
    size_t roll_coverage=0;
    unsigned rate_index = UINT32_MAX, depth = 0, shift, published_revision = UINT32_MAX;
    unsigned single_arm = UINT32_MAX, previous_baseline_mask = 0;
    unsigned observed_arm=UINT32_MAX;
    uint32_t probe_control=UINT32_MAX;
    struct osc_capture_dsp dsp={0};
    uint32_t dsp_sequence=0;
    uint64_t dsp_event=CAPTURE_NO_TRIGGER,dsp_latest_end=0;
    int dsp_initialized=0,dsp_rearm=0;
    debug.data = p->storage[3]; debug.capacity = CAPTURE_MAX_SAMPLES * 2;
    capture_history_init(&history, p->storage[0], p->storage[0]+HARDWARE_HISTORY_SAMPLES*2, 100000);
    while (!stopped(p)) {
        CaptureRequest request;
        unsigned revision, arm, source_revision, wanted_depth, wanted_rate, history_depth;
        int failed, force,awaiting_presentation;
        scope_mutex_lock(&p->mutex);
        request = p->request; revision = p->revision; arm = p->arm_revision;
        source_revision = p->source_revision;
        failed = p->source_error;
        while(presentation_count && pending_presentation[0]<=p->presented_id){
            pending_presentation[0]=pending_presentation[1];--presentation_count;
        }
        awaiting_presentation=p->presentation_sync && presentation_count==2;
        scope_mutex_unlock(&p->mutex);
        if(observed_arm!=arm){presentation_count=0;awaiting_presentation=0;observed_arm=arm;dsp_rearm=1;}
        if (request.duration_seconds <= 0) { scope_sleep_ms(5); continue; }
        wanted_depth = request.record_samples ? request.record_samples : 100000;
        if (wanted_depth > CAPTURE_MAX_SAMPLES) wanted_depth = CAPTURE_MAX_SAMPLES;
        wanted_rate = capture_rate_index(fmax(request.duration_seconds, request.overview_duration_seconds),
                                         wanted_depth, request.roll);
        shift = capture_payload_shift_for_depth(osc_capture_rate(wanted_rate),wanted_depth);
        struct osc_capture_dsp wanted_dsp=requested_dsp(p,&request,osc_capture_rate(wanted_rate));
        uint32_t wanted_probe=wanted_dsp.control?0:requested_probe(p,&request);
        history_depth = wanted_depth + (request.roll ? 0 :
            ((osc_capture_rate(wanted_rate)==200000000?4u:2u) << shift)*(wanted_dsp.control?2u:1u));
        force = request.force_sequence != forced;
        int source_changed=memcmp(&previous_debug,&request.debug,sizeof(previous_debug)) ||
            previous_baseline_mask!=request.baseline_mask;
        if (source_changed ||
            memcmp(previous_gain, request.adc_volts_per_code, sizeof(previous_gain))) {
            if(!release_packets(p,&packets)){
                failed=1;scope_mutex_lock(&p->mutex);p->source_error=1;scope_mutex_unlock(&p->mutex);
            }
            capture_history_init(&history, p->storage[0], p->storage[0]+HARDWARE_HISTORY_SAMPLES*2, history_depth);
            last_display = 0;roll_coverage=0; previous_debug = request.debug;
            presentation_count=0;awaiting_presentation=0;
            previous_baseline_mask=request.baseline_mask;
            memcpy(previous_gain, request.adc_volts_per_code, sizeof(previous_gain));
        }
        if ((request.running || force) && (wanted_rate != rate_index || wanted_depth != depth ||
            history.capacity != history_depth || wanted_probe!=probe_control || !dsp_initialized || !same_dsp(&wanted_dsp,&dsp))) {
            wanted_dsp.arm_id=++dsp_sequence;
            int configured=release_packets(p,&packets) && zynq_capture_set_dsp(&p->zynq,&wanted_dsp);
            if(p->zynq.dsp_supported<0){
                memset(&wanted_dsp,0,sizeof(wanted_dsp));wanted_probe=requested_probe(p,&request);
                history_depth=wanted_depth+(request.roll?0:((osc_capture_rate(wanted_rate)==200000000?4u:2u)<<shift));
            }
            if (!configured || !zynq_capture_set_probe(&p->zynq,wanted_probe) ||
                !zynq_capture_configure(&p->zynq, wanted_rate, shift)) failed = 1;
            else {
                dsp=wanted_dsp;dsp_initialized=1;dsp_rearm=0;dsp_event=CAPTURE_NO_TRIGGER;dsp_latest_end=0;
                rate_index = wanted_rate; depth = wanted_depth;
                probe_control=p->zynq.probe_supported<0?0:wanted_probe;
                capture_history_init(&history, p->storage[0], p->storage[0]+HARDWARE_HISTORY_SAMPLES*2, history_depth);
                expected = next_trigger = last_display = 0;roll_coverage=0; failed = 0;
                presentation_count=0;awaiting_presentation=0;
            }
            scope_mutex_lock(&p->mutex);
            p->source_error = failed; p->adc_sample_ns = 0;
            scope_mutex_unlock(&p->mutex);
        }
        if(!failed && dsp.control && dsp_rearm && !awaiting_presentation &&
            (force || (request.running && (request.trigger_mode!=2 || single_arm!=arm)))){
            dsp.arm_id=++dsp_sequence;dsp.minimum_sample=dsp_latest_end>next_trigger?dsp_latest_end:next_trigger;
            if(!zynq_capture_set_dsp(&p->zynq,&dsp)){
                failed=1;scope_mutex_lock(&p->mutex);p->source_error=1;scope_mutex_unlock(&p->mutex);
            }else {dsp_rearm=0;dsp_event=CAPTURE_NO_TRIGGER;}
        }
        if (!failed && (force || (request.running && (request.trigger_mode != 2 || single_arm != arm)))) {
            CaptureBuffer received;
            uint64_t acquiring=scope_clock_ns();
            size_t incoming=(osc_capture_rate(wanted_rate)==200000000?4u:2u)<<shift;
            ZynqCapturePacket newest,packet,preceding;
            int result=zynq_capture_dequeue(&p->zynq,&newest,40);
            if (result < 0) {
                scope_mutex_lock(&p->mutex); p->source_error = 1; scope_mutex_unlock(&p->mutex);
            } else if (result > 0) {
                uint64_t started = scope_clock_ns();
                preceding=newest;
                dsp_latest_end=newest.metadata.first_sample+newest.metadata.bytes/2;
                int hardware_dsp=!!dsp.control;
                if(hardware_dsp && !newest.dsp_valid){
                    zynq_capture_release_index(&p->zynq,newest.index);
                    scope_mutex_lock(&p->mutex);p->source_error=1;scope_mutex_unlock(&p->mutex);continue;
                }
                int current_dsp=hardware_dsp && newest.dsp_arm_id==dsp.arm_id && newest.dsp_control==dsp.control;
                if(current_dsp){
                    scope_mutex_lock(&p->mutex);++p->hardware_metrics.dsp_blocks;scope_mutex_unlock(&p->mutex);
                    if(newest.dsp_triggered && !dsp_rearm)dsp_event=newest.dsp_sample;
                }
                unsigned encoded_context_limit=(depth+incoming-1)/incoming+1;
                int dsp_fast=hardware_dsp && encoded_context_limit<=5;
                int fast=!hardware_dsp && !request.roll && !request.debug.enabled && !request.baseline_mask && !request.calibration_mask && depth<=incoming;
                int bounds=0,possible=1,current_possible=1;
                if(packets.pending_count){
                    packet=packets.pending[0];
                    preceding=packets.pending[packets.pending_count-1];
                    bounds=newest.bounds_valid && newest.bounds_sequence==packet.metadata.sequence;
                    if(fast && newest.bounds_valid && !bounds &&
                       packets.pending_count<(newest.window_bounds?2u:1u)){
                        packets.pending[packets.pending_count++]=newest;continue;
                    }
                    /* A delayed consumer can lose the header carrying the
                       pending packet's summary. Resynchronise without decoding
                       stale payloads, otherwise their cost perpetuates the gap. */
                    if(fast && newest.bounds_valid && !bounds){
                        int released=1;
                        for(unsigned i=0;i<packets.pending_count;++i)
                            released&=zynq_capture_release_index(&p->zynq,packets.pending[i].index);
                        packets.pending[0]=newest;packets.pending_count=1;
                        if(packets.have_context){
                            released&=zynq_capture_release_index(&p->zynq,packets.context.index);
                            packets.have_context=0;
                        }
                        packets.post=0;
                        scope_mutex_lock(&p->mutex);
                        p->hardware_metrics.acquisition_ns+=scope_clock_ns()-acquiring;
                        if(!released)p->source_error=1;
                        scope_mutex_unlock(&p->mutex);continue;
                    }
                    if(packets.pending_count==2)packets.pending[0]=packets.pending[1];
                    packets.pending[packets.pending_count-1]=newest;
                }else if(fast && newest.bounds_valid){
                    packets.pending[0]=newest;packets.pending_count=1;continue;
                }else packet=newest;
                CaptureBuffer calibrated=packet.metadata;
                for(unsigned ch=0;ch<2;++ch)if(request.adc_volts_per_code[ch]>0)calibrated.volts_per_code[ch]=request.adc_volts_per_code[ch];
                calibrated=calibrated_buffer(&calibrated,&request);
                unsigned trigger_padding=signal_filter_padding(request.filter_level[request.trigger_channel],
                    channel_baseline_noise(capture_baseline(&request,&calibrated,(unsigned)request.trigger_channel),
                        calibrated.volts_per_code[request.trigger_channel])>0);
                if(bounds){
                    unsigned sides=packet_trigger_sides(&newest,&calibrated,&request,probe_control);
                    int previous=preceding.bounds_valid && preceding.bounds_sequence+1==packet.metadata.sequence;
                    current_possible=sides==3;
                    if(previous)sides|=packet_trigger_sides(&preceding,&calibrated,&request,probe_control);
                    /* Every accepted edge needs both sides somewhere in the
                       retained history, even when only one direction is selected. */
                    if(previous)possible=sides==3;
                    if(newest.probe_valid && newest.probe_control==probe_control){
                        scope_mutex_lock(&p->mutex);++p->hardware_metrics.probe_blocks;scope_mutex_unlock(&p->mutex);
                    }
                }
                int synchronize=!request.roll && !request.debug.enabled && !request.baseline_mask && !request.calibration_mask && !force;
                int busy=synchronize && awaiting_presentation;
                if(busy){
                    uint64_t fresh=newest.metadata.first_sample+newest.metadata.bytes/2;
                    if(fresh>trigger_padding)fresh-=trigger_padding;
                    if(next_trigger<fresh)next_trigger=fresh;
                }
                int deadtime=synchronize && (busy || packet.metadata.first_sample+packet.metadata.bytes/2<=next_trigger);
                int auto_due=request.trigger_mode==0 && (revision!=published_revision || started-last_display>=40000000);
                /* Preserve a complete contiguous record before its first bulk
                   decode. At large depth, decoding one incomplete payload can
                   take longer than the next payload and perpetually reset history. */
                int collecting=dsp_fast && depth>incoming && retained_samples(&packets,&packet,&history)<depth;
                int skip=collecting || deadtime || (((fast && bounds && !possible && !packets.post) ||
                    (dsp_fast && (dsp_event==CAPTURE_NO_TRIGGER || dsp_rearm || !current_dsp))) && !force && !auto_due);
                int drain_history=deadtime && depth>incoming && !dsp_fast;
                if(skip && !drain_history){
                    uint8_t data[4],peaks[4];CaptureBuffer tail;
                    size_t samples=packet.metadata.bytes/2,count=samples>=2?2:1;
                    if(!zynq_capture_decode_packet(&p->zynq,&packet,samples-count,count,&tail,data,peaks,sizeof(data))){
                        zynq_capture_release_index(&p->zynq,packet.index);
                        scope_mutex_lock(&p->mutex);p->source_error=1;scope_mutex_unlock(&p->mutex);continue;
                    }
                    count_hardware_block(p,&packet.metadata,tail.data+tail.bytes-2,started,&expected);
                    if(dsp_fast){if(!keep_encoded_context(p,&packets,&packet,encoded_context_limit))failed=1;}
                    else {
                        if(packets.have_context && !zynq_capture_release_index(&p->zynq,packets.context.index))failed=1;
                        packets.context=packet;packets.have_context=1;
                    }
                    scope_mutex_lock(&p->mutex);
                    if(deadtime){++p->hardware_metrics.deadtime_blocks;packets.post=0;}
                    else if(!collecting)++p->hardware_metrics.rejected_blocks;
                    p->hardware_metrics.acquisition_ns+=scope_clock_ns()-acquiring;
                    p->hardware_metrics.waiting=!busy && request.trigger_mode!=0;
                    if(failed)p->source_error=1;
                    scope_mutex_unlock(&p->mutex);continue;
                }
                packets.post=bounds?current_possible:1;
                for(unsigned i=0;i<packets.encoded_count;++i){
                    CaptureBuffer preceding;
                    if(!decode_history(p,&history,&packets.encoded_context[i],&request,0,&preceding))failed=1;
                    else capture_history_append(&history,&preceding);
                    if(!zynq_capture_release_index(&p->zynq,packets.encoded_context[i].index))failed=1;
                }
                packets.encoded_count=0;
                if(packets.have_context){
                    CaptureBuffer preceding;
                    if(!decode_history(p,&history,&packets.context,&request,depth,&preceding))failed=1;
                    else capture_history_append(&history,&preceding);
                    if(!zynq_capture_release_index(&p->zynq,packets.context.index))failed=1;
                    packets.have_context=0;
                }
                if(!decode_history(p,&history,&packet,&request,0,&received))failed=1;
                if(failed){
                    zynq_capture_release_index(&p->zynq,packet.index);
                    scope_mutex_lock(&p->mutex);p->source_error=1;scope_mutex_unlock(&p->mutex);continue;
                }
                count_hardware_block(p,&packet.metadata,received.data+received.bytes-2,started,&expected);
                if (request.debug.enabled) {
                    debug.peaks=debug.data+debug.capacity;
                    if (!capture_debug_replace(&received, &debug, &request.debug)) failed = 1;
                    else capture_history_append(&history, &debug);
                } else capture_history_append(&history, &received);
                if (!zynq_capture_release_index(&p->zynq,packet.index)) failed = 1;
                if (failed) {
                    scope_mutex_lock(&p->mutex); p->source_error = 1; scope_mutex_unlock(&p->mutex);
                    continue;
                }
                if(drain_history){
                    scope_mutex_lock(&p->mutex);
                    ++p->hardware_metrics.deadtime_blocks;
                    p->hardware_metrics.acquisition_ns+=scope_clock_ns()-acquiring;
                    p->hardware_metrics.waiting=!busy && request.trigger_mode!=0;
                    scope_mutex_unlock(&p->mutex);continue;
                }
                uint64_t searching=scope_clock_ns();
                /* Search each admitted block, including AUTO. Do not batch
                   them before finding its first eligible crossing. */
                {
                    int triggered = 0, complete,slot=capture_ring_begin(&p->ring);
                    double span, pre, post;
                    if(slot<0)continue;
                    capture_history_view(&history,&candidate);
                    span = fmax(request.duration_seconds, request.overview_duration_seconds) * candidate.sample_rate_hz;
                    pre = ceil(span * (1.0 - request.pan_fraction))+trigger_padding;
                    post = ceil(span * request.pan_fraction)+trigger_padding;
                    complete = request.roll || (candidate.bytes / 2 >= pre + post &&
                        (!force || candidate.bytes / 2 > pre));
                    size_t coverage=candidate.bytes/2;
                    if(request.roll){
                        if(coverage>ceil(span))coverage=(size_t)ceil(span);
                        /* A gap starts a new contiguous history. Keep the last
                           visible sweep until the replacement covers as much
                           time; never collapse it to a short tail. */
                        complete=coverage>=roll_coverage;
                    }
                    if(hardware_dsp && candidate.bytes/2<depth)complete=0;
                    if (!request.roll) {
                        if (complete) {
                            CaptureBuffer search = calibrated_buffer(&candidate,&request);
                            search.peaks=NULL;
                            uint64_t first = candidate.first_sample + (uint64_t)pre;
                            if (first < next_trigger) first = next_trigger;
                            if(hardware_dsp){
                                if(dsp_event!=CAPTURE_NO_TRIGGER && dsp_event<first){
                                    dsp_rearm=1;dsp_event=CAPTURE_NO_TRIGGER;
                                    scope_mutex_lock(&p->mutex);++p->hardware_metrics.dsp_missed;scope_mutex_unlock(&p->mutex);
                                }
                                triggered=dsp_event!=CAPTURE_NO_TRIGGER;
                                candidate.trigger_sample=triggered?dsp_event:CAPTURE_NO_TRIGGER;
                            }else triggered = capture_find_trigger_filtered(&search, request.trigger_channel, request.trigger_edge,
                                request.trigger_voltage, 2, request.filter_level[request.trigger_channel],
                                request.baseline_mask || request.calibration_mask?0:channel_baseline_noise(capture_baseline(&request,&candidate,(unsigned)request.trigger_channel),candidate.volts_per_code[request.trigger_channel]),first,&candidate.trigger_sample);
                            /* The filter needs available posttrigger samples too.
                               Reject a late edge until its whole display follows. */
                            if(triggered && candidate.trigger_sample-candidate.first_sample>
                               candidate.bytes/2-(size_t)post){
                                triggered=0;candidate.trigger_sample=CAPTURE_NO_TRIGGER;
                            }
                        }
                    }
                    if (force && complete) {
                        size_t anchor = candidate.bytes / 4;
                        if (!request.roll) {
                            if (anchor < pre) anchor = (size_t)pre;
                            if (anchor > candidate.bytes / 2 - post)
                                anchor = candidate.bytes / 2 - (size_t)post;
                        }
                        candidate.trigger_sample = candidate.first_sample + anchor;
                        triggered = 1; forced = request.force_sequence;
                    }
                    int present=complete && (triggered || (request.trigger_mode==0 &&
                        (revision!=published_revision || started-last_display>=40000000)));
                    scope_mutex_lock(&p->mutex);
                    uint64_t searched=scope_clock_ns()-searching;
                    p->hardware_metrics.acquisition_ns+=searching-acquiring;
                    p->hardware_metrics.search_ns+=searched;
                    if(searched>p->hardware_metrics.search_max_ns)p->hardware_metrics.search_max_ns=searched;
                    p->hardware_metrics.waiting = !triggered && request.trigger_mode != 0;
                    if(present){
                        candidate.accepted_ns=scope_clock_ns();p->hardware_metrics.triggered_ns=candidate.accepted_ns;
                    }
                    scope_mutex_unlock(&p->mutex);
                    if (present) {
                        if(hardware_dsp && (triggered || force))dsp_rearm=1;
                        retain_record(&candidate,depth,request.pan_fraction,(size_t)pre,(size_t)post);
                        CaptureBuffer owned=p->ring.buffer[slot];
                        uint8_t *peaks=owned.data+owned.capacity;
                        memcpy(owned.data,candidate.data,candidate.bytes);
                        if(candidate.peaks)memcpy(peaks,candidate.peaks,candidate.bytes);
                        candidate.data=owned.data;candidate.capacity=owned.capacity;
                        candidate.peaks=candidate.peaks?peaks:NULL;
                        if (triggered) {
                            uint64_t holdoff_end=candidate.trigger_sample+
                                request.holdoff_samples*candidate.sample_rate_hz/200000000+1;
                            /* Re-arm at new input, not at a later edge in the
                               already searched DMA batch retained as history. */
                            uint64_t consumed=packet.metadata.first_sample+packet.metadata.bytes/2;
                            /* Final centred outputs still need the next block;
                               their crossings have not been examined in full. */
                            if(consumed>trigger_padding)consumed-=trigger_padding;
                            next_trigger=holdoff_end>consumed?holdoff_end:consumed;
                        }
                        if (request.trigger_mode == 2) single_arm = arm;
                        last_display = started;
                        p->ring.buffer[slot]=candidate;p->record_source[slot]=source_revision;
                        if(!capture_ring_publish(&p->ring,slot))capture_ring_cancel(&p->ring,slot);
                        else {
                            if(request.roll)roll_coverage=coverage;
                            published_revision=revision;
                            if(presentation_count<2)pending_presentation[presentation_count++]=candidate.accepted_ns;
                        }
                    }else capture_ring_cancel(&p->ring,slot);
                }
            }
        } else {release_packets(p,&packets);scope_sleep_ms(5);}
    }
    release_packets(p,&packets);
}
CapturePipeline *capture_pipeline_create_zynq(const char *device, const double gain[2],
                                               const double zero[2])
{
    CapturePipeline *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (!zynq_capture_open(&p->zynq, device, gain, zero)) { free(p); return NULL; }
    if(!zynq_analysis_open(&p->analysis,"/dev/oscill-analysis")){
        zynq_capture_close(&p->zynq);free(p);return NULL;
    }
    p->hardware = 1;
    {
        unsigned i;
        for (i = 0; i < 5; ++i) {
            p->storage[i] = malloc((i==0?HARDWARE_HISTORY_SAMPLES:CAPTURE_MAX_SAMPLES) * 4);
            if (!p->storage[i]) goto fail;
        }
    }
    scope_mutex_init(&p->mutex);
    {
        uint8_t *records[]={p->storage[1],p->storage[2],p->storage[4]};
        capture_ring_init(&p->ring,records,3,CAPTURE_MAX_SAMPLES*2,CAPTURE_OVERWRITE_READY);
    }
    /* Start only after the frontend has supplied its first request. */
    if (!scope_thread_start(&p->processing, processing_worker, p)) {
        capture_ring_destroy(&p->ring);scope_mutex_destroy(&p->mutex);goto fail;
    }
    if (!scope_thread_start(&p->acquisition, hardware_worker, p)) {
        scope_mutex_lock(&p->mutex);p->stop=1;scope_mutex_unlock(&p->mutex);
        capture_ring_stop(&p->ring);scope_thread_join(&p->processing);
        capture_ring_destroy(&p->ring);scope_mutex_destroy(&p->mutex);goto fail;
    }
    return p;
fail:
    for(unsigned i=0;i<5;++i)free(p->storage[i]);
    zynq_analysis_close(&p->analysis);
    zynq_capture_close(&p->zynq);free(p);return NULL;
}
#endif
CapturePipeline *capture_pipeline_create(const char *path)
{
    CapturePipeline *p = calloc(1, sizeof(*p));
    size_t i, count = 8, bytes = 2 * 1024 * 1024;
    if (!p)
        return NULL;
    if (path) {
        p->file = fopen(path, "rb");
        if (!p->file || !file_capture_source_init(&p->source, &p->file_source, p->file))
            goto fail;
        bytes = p->file_source.metadata.bytes;
        count = 1;
        if (bytes > 256 * 1024 * 1024)
            goto fail;
    } else
        demo_capture_source_init(&p->source, &p->demo, 200000000);
    for (i = 0; i < count; ++i) {
        p->storage[i] = malloc(bytes);
        if (!p->storage[i])
            goto fail;
    }
    scope_mutex_init(&p->mutex);
    capture_ring_init(&p->ring, p->storage, count, bytes, CAPTURE_OVERWRITE_READY);
    p->request.duration_seconds = 0.001;
    p->request.pan_fraction = 0.5;
    p->request.running = 1;
    p->request.zero_y[0] = 200;
    p->request.zero_y[1] = 330;
    p->request.pixels_per_volt[0] = 120;
    p->request.pixels_per_volt[1] = 60;
    if (!scope_thread_start(&p->acquisition, acquisition_worker, p))
        goto fail_ring;
    if (!scope_thread_start(&p->processing, processing_worker, p)) {
        scope_mutex_lock(&p->mutex);
        p->stop = 1;
        scope_mutex_unlock(&p->mutex);
        scope_thread_join(&p->acquisition);
        goto fail_ring;
    }
    return p;
fail_ring:
    capture_ring_destroy(&p->ring);
    scope_mutex_destroy(&p->mutex);
fail:
    for (i = 0; i < CAPTURE_RING_MAX; ++i)
        free(p->storage[i]);
    if (p->file)
        fclose(p->file);
    free(p);
    return NULL;
}
void capture_pipeline_destroy(CapturePipeline *p)
{
    size_t i;
    if (!p)
        return;
    scope_mutex_lock(&p->mutex);
    p->stop = 1;
    scope_mutex_unlock(&p->mutex);
    capture_ring_stop(&p->ring);
    scope_thread_join(&p->acquisition);
    scope_thread_join(&p->processing);
    capture_ring_destroy(&p->ring);
#ifdef SCOPE_ENABLE_ZYNQ
    if (p->hardware) {zynq_capture_close(&p->zynq);zynq_analysis_close(&p->analysis);}
#endif
    scope_mutex_destroy(&p->mutex);
    for (i = 0; i < CAPTURE_RING_MAX; ++i)
        free(p->storage[i]);
    if (p->file)
        fclose(p->file);
    free(p);
}
void capture_pipeline_request(CapturePipeline *p, const CaptureRequest *r)
{
    unsigned ch;
    if (!p || !r || !isfinite(r->duration_seconds) || r->duration_seconds <= 0 ||
        !isfinite(r->pan_fraction) || r->pan_fraction < 0 || r->pan_fraction > 1 ||
        (r->hidden_channel_mask&~3u))
        return;
    for (ch = 0; ch < 2; ++ch)
        if (!isfinite(r->zero_y[ch]) || !isfinite(r->pixels_per_volt[ch]) || r->filter_level[ch] > 3 ||
            r->range_stage[ch]>=INPUT_RANGE_STAGE_COUNT ||
            (r->response_enabled[ch] && (!channel_response_valid(&r->response[ch]) || r->response[ch].count<2)))
            return;
    for(unsigned ch=0;ch<2;++ch)for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)
        if(!channel_baseline_valid(&r->baseline[ch][stage]))return;
    if (!isfinite(r->overview_duration_seconds) || r->overview_duration_seconds < 0 ||
        !isfinite(r->zoom_offset_seconds) || !isfinite(r->trigger_voltage) ||
        r->trigger_channel < 0 || r->trigger_channel > 1 ||
        r->trigger_edge < 0 || r->trigger_edge > 2 ||
        r->trigger_mode < 0 || r->trigger_mode > 2)
        return;
    scope_mutex_lock(&p->mutex);
    if((r->running && !p->request.running) || p->request.force_sequence!=r->force_sequence ||
       p->request.baseline_mask!=r->baseline_mask || memcmp(&p->request.debug,&r->debug,sizeof(r->debug)))
        p->acquired_id=p->presented_id;
    if(p->request.baseline_mask!=r->baseline_mask || memcmp(&p->request.debug,&r->debug,sizeof(r->debug)))
        ++p->source_revision;
    if (r->running && !p->request.running) ++p->arm_revision;
    if (p->request.duration_seconds != r->duration_seconds ||
        p->request.calibration_mask != r->calibration_mask ||
        p->request.baseline_mask != r->baseline_mask ||
        memcmp(p->request.baseline,r->baseline,sizeof(r->baseline)) ||
        memcmp(p->request.range_stage,r->range_stage,sizeof(r->range_stage)) ||
        p->request.spectrum_enabled != r->spectrum_enabled ||
        p->request.density_enabled != r->density_enabled ||
        p->request.density_rows != r->density_rows ||
        p->request.hidden_channel_mask != r->hidden_channel_mask ||
        memcmp(p->request.filter_level, r->filter_level, sizeof(r->filter_level)) ||
        memcmp(p->request.response_enabled,r->response_enabled,sizeof(r->response_enabled)) ||
        memcmp(p->request.response,r->response,sizeof(r->response)) ||
        p->request.roll != r->roll || p->request.record_samples != r->record_samples ||
        p->request.overview_duration_seconds != r->overview_duration_seconds ||
        p->request.zoom_offset_seconds != r->zoom_offset_seconds ||
        p->request.pan_fraction != r->pan_fraction ||
        memcmp(p->request.zero_y, r->zero_y, sizeof(r->zero_y)) ||
        memcmp(p->request.pixels_per_volt, r->pixels_per_volt, sizeof(r->pixels_per_volt)))
        ++p->revision;
    p->request = *r;
    scope_mutex_unlock(&p->mutex);
}
const DisplayFrame *capture_pipeline_acquire(CapturePipeline *p)
{
    unsigned slot;
    scope_mutex_lock(&p->mutex);slot=p->ready_slot;
    if(p->presentation_sync && p->acquired_id>p->presented_id)slot=0;
    if(slot){p->ready_slot=0;p->reading_slot=slot;p->acquired_id=p->frames[slot-1].capture_id;}
    scope_mutex_unlock(&p->mutex);
    return slot?&p->frames[slot-1]:NULL;
}

void capture_pipeline_retain_frame(CapturePipeline *p,const DisplayFrame *frame)
{
    scope_mutex_lock(&p->mutex);
    for(unsigned i=0;i<3;++i)if(frame==&p->frames[i]){++p->frame_pins[i];break;}
    scope_mutex_unlock(&p->mutex);
}

void capture_pipeline_release_frame(CapturePipeline *p,const DisplayFrame *frame)
{
    scope_mutex_lock(&p->mutex);
    for(unsigned i=0;i<3;++i)if(frame==&p->frames[i]){if(p->frame_pins[i])--p->frame_pins[i];break;}
    scope_mutex_unlock(&p->mutex);
}
void capture_pipeline_presented(CapturePipeline *p,uint64_t capture_id)
{
    if(!p)return;
    scope_mutex_lock(&p->mutex);
    p->presentation_sync=1;
    if(capture_id>p->presented_id)p->presented_id=capture_id;
    scope_mutex_unlock(&p->mutex);
}

/* One GUI consumer leases a completed slot. Neither copying the density map
   nor processing a new frame holds the configuration/metrics mutex. */
int capture_pipeline_frame(CapturePipeline *p, DisplayFrame *f)
{
    unsigned slot;
    const DisplayFrame *ready;
    scope_mutex_lock(&p->mutex);
    slot=p->ready_slot;
    if(p->presentation_sync && p->acquired_id>p->presented_id)slot=0;
    if(slot){p->ready_slot=0;p->reading_slot=slot;p->acquired_id=p->frames[slot-1].capture_id;}
    scope_mutex_unlock(&p->mutex);
    if(!slot)return 0;
    ready=&p->frames[slot-1];
    memcpy(f,ready,ready->density_valid?sizeof(*f):offsetof(DisplayFrame,connections));
    scope_mutex_lock(&p->mutex);
    p->reading_slot=0;
    scope_mutex_unlock(&p->mutex);
    return 1;
}
void capture_pipeline_metrics(CapturePipeline *p, CaptureMetrics *m)
{
    size_t occupancy,peak_occupancy;
    uint64_t overwritten;
    memset(m, 0, sizeof(*m));
    scope_mutex_lock(&p->ring.mutex);
    if(!p->hardware){m->blocks=p->ring.published;m->dropped=p->ring.dropped;}
    overwritten=p->ring.overwritten;
    occupancy=p->ring.occupancy;
    peak_occupancy=p->ring.peak_occupancy;
    scope_mutex_unlock(&p->ring.mutex);
    scope_mutex_lock(&p->mutex);
#ifdef SCOPE_ENABLE_ZYNQ
    if (p->hardware) *m = p->hardware_metrics;
#endif
    m->overwritten=overwritten;m->occupancy=occupancy;m->peak_occupancy=peak_occupancy;
    m->bytes = p->bytes;
    m->display_replaced = p->display_replaced;
    m->processing_ns = p->processing_ns;
    m->waveforms = p->waveforms;
    m->source_error = p->source_error;
    m->adc_valid = p->hardware && p->adc_sample_ns && !p->source_error;
    m->adc_age_ms = m->adc_valid ? (scope_clock_ns() - p->adc_sample_ns) / 1000000 : UINT64_MAX;
    scope_mutex_unlock(&p->mutex);
}
