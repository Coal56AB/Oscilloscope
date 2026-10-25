#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_zynq.h"
#include "capture_pipeline.h"
#include "capture_selftest.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t memory[4 * 4096];
static atomic_uint leased, available, counter, releases, configurations;
static atomic_int fail_poll, bad_offset, bad_info;
static atomic_uint sample_rate = 200000000;
static atomic_uint adc_pair = 42 | (42 << 8);
static atomic_int poison_release;
static atomic_int packed_peaks;
static int selftest_mode, selftest_fail_shift=-1, selftest_rail=-1;
static unsigned selftest_clocks, selftest_progress_count;
static unsigned selftest_poll_fail_rate;
int __wrap_open(const char *path, int flags, ...) { (void)flags; assert(!strcmp(path,"/dev/test-adc")); return 42; }
int __wrap_close(int fd) { assert(fd==42); atomic_store(&leased,0); return 0; }
void *__wrap_mmap(void *addr, size_t length, int protection, int flags, int fd, long offset)
{
    (void)addr; assert(length==sizeof(memory)&&protection==PROT_READ&&flags==MAP_SHARED&&fd==42&&!offset);
    return memory;
}
int __wrap_munmap(void *addr, size_t length) { assert(addr==memory&&length==sizeof(memory)); return 0; }
int __wrap_poll(struct pollfd *p, unsigned long count, int timeout)
{
    assert(count==1&&p->fd==42);
    if(selftest_mode && atomic_load(&sample_rate)==selftest_poll_fail_rate){p->revents=POLLERR;return 1;}
    if (atomic_load(&fail_poll)) { p->revents=POLLERR; return 1; }
    if (atomic_load(&available)) { p->revents=POLLIN; return 1; }
    scope_sleep_ms(timeout>5?5:timeout); return 0;
}
int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list args; void *arg;
    assert(fd==42); va_start(args,command); arg=va_arg(args,void *); va_end(args);
    if(command==OSC_CAPTURE_GET_INFO) {
        struct osc_capture_info info={OSC_CAPTURE_ABI,OSC_CAPTURE_REGISTER_VERSION,OSC_CAPTURE_SAMPLE_FORMAT,1,4,4096,sizeof(memory)};
        if(atomic_load(&bad_info))info.buffers=1000;
        *(struct osc_capture_info *)arg=info;
    } else if(command==OSC_CAPTURE_CONFIGURE) {
        struct osc_capture_config *config=arg;
        assert(!atomic_load(&leased));
        sample_rate=osc_capture_rate(config->rate_index); assert(sample_rate);
        atomic_store(&counter,0);
        atomic_fetch_add(&configurations,1);
    } else if(command==OSC_CAPTURE_TEST_CLOCK) {
        const struct osc_capture_test_clock *config=arg;
        assert(!atomic_load(&leased) && config->shift<=6 && config->payload_shift==17);
        assert(config->shift==6-selftest_clocks);
        if((int)config->shift==selftest_fail_shift){errno=EIO;return -1;}
        sample_rate=200000000u>>config->shift;
        atomic_store(&counter,0);atomic_store(&available,4);++selftest_clocks;
    } else if(command==OSC_CAPTURE_DEQUEUE) {
        struct osc_capture_block *b=arg;
        unsigned i,mask=atomic_load(&leased),sequence;
        if(!atomic_load(&available)){errno=EAGAIN;return -1;}
        for(i=0;i<4&&(mask&(1u<<i));++i){}
        assert(i<4); atomic_fetch_or(&leased,1u<<i); atomic_fetch_sub(&available,1);
        sequence=atomic_fetch_add(&counter,1);
        memset(b,0,sizeof(*b)); b->index=i;b->bytes=256;b->data_offset=atomic_load(&bad_offset)?4090:64;
        b->sequence=sequence;b->sample_rate_hz=sample_rate;b->first_sample=(uint64_t)sequence*128;
        b->trigger_sample=CAPTURE_NO_TRIGGER;b->hardware_ns=b->first_sample*5;
        memset(memory+i*4096+64,42,256);
        memory[i*4096+64+254]=(uint8_t)atomic_load(&adc_pair);
        memory[i*4096+64+255]=(uint8_t)(atomic_load(&adc_pair)>>8);
        if(selftest_mode && selftest_rail>=0)
            for(unsigned pos=(unsigned)selftest_rail;pos<256;pos+=4)memory[i*4096+64+pos]=255;
        if(atomic_load(&packed_peaks)){
            uint8_t *data=memory+i*4096+64;
            b->flags=OSC_CAPTURE_PEAKS;b->first_sample=(uint64_t)sequence*64;
            for(unsigned pos=0;pos<256;pos+=8){
                const uint8_t group[]={128,129,130,131,10,20,240,250};
                memcpy(data+pos,group,8);
            }
        }
    } else if(command==OSC_CAPTURE_RELEASE) {
        unsigned index=*(uint32_t *)arg;
        assert(index<4&&(atomic_fetch_and(&leased,~(1u<<index))&(1u<<index)));
        atomic_fetch_add(&releases,1);
        if(atomic_load(&poison_release))memset(memory+index*4096+64,238,256);
    } else assert(0);
    return 0;
}
static int wait_frame(CapturePipeline *p, DisplayFrame *f)
{
    unsigned i;
    for(i=0;i<100;++i){if(capture_pipeline_frame(p,f))return 1;scope_sleep_ms(5);}
    return 0;
}
static void expect_no_frame(CapturePipeline *p, unsigned completed)
{
    static DisplayFrame frame;
    unsigned i;
    for (i=0;i<100 && atomic_load(&releases)<completed;++i) {
        assert(!capture_pipeline_frame(p,&frame));
        scope_sleep_ms(5);
    }
    assert(atomic_load(&releases)>=completed);
    scope_sleep_ms(50);
    assert(!capture_pipeline_frame(p,&frame));
}
static void complete_force_frame_test(void)
{
    double gain[2]={0.004,0.004},zero[2]={128,128};
    CaptureRequest request={0};
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    static DisplayFrame frame;
    unsigned configured=atomic_load(&configurations),completed=atomic_load(&releases);
    assert(p);
    request.duration_seconds=0.256;request.record_samples=512;request.force_sequence=1;
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    assert(atomic_load(&configurations)>configured&&atomic_load(&sample_rate)==1000);
    /* With the marker at the right edge, FORCE needs the marker sample as
       well as all 256 preceding samples of the visible interval. */
    atomic_store(&available,2);expect_no_frame(p,completed+2);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    for(unsigned ch=0;ch<2;++ch)for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)
        assert(frame.y[ch][x]!=INT16_MIN);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
}
static void complete_auto_frame_test(double seconds, int zoom)
{
    double gain[2]={0.004,0.004},zero[2]={128,128};
    CaptureRequest request={0};
    static DisplayFrame frame;
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    unsigned rate, blocks, completed, configured=atomic_load(&configurations);
    assert(p);
    request.running=1;request.duration_seconds=seconds;
    if(zoom){request.overview_duration_seconds=seconds;request.duration_seconds/=4;}
    request.record_samples=1000;request.pan_fraction=0.5;request.trigger_voltage=100;
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    assert(atomic_load(&configurations)>configured);
    rate=atomic_load(&sample_rate);
    blocks=(unsigned)ceil(seconds*rate/128);
    assert(blocks>1);
    completed=atomic_load(&releases);
    atomic_store(&available,blocks-1);
    expect_no_frame(p,completed+blocks-1);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    assert(frame.view_samples>=(size_t)ceil(request.duration_seconds*rate));
    assert(frame.view_samples<=(size_t)ceil(request.duration_seconds*rate)+1);
    for(unsigned ch=0;ch<2;++ch)for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)
        assert(frame.y[ch][x]!=INT16_MIN);
    /* A discontinuity must accumulate a complete interval again. */
    atomic_fetch_add(&counter,100);
    completed=atomic_load(&releases);
    atomic_store(&available,blocks-1);
    expect_no_frame(p,completed+blocks-1);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    assert(frame.view_samples>=(size_t)ceil(request.duration_seconds*rate));
    assert(frame.view_samples<=(size_t)ceil(request.duration_seconds*rate)+1);
    if(!zoom&&seconds==1.0) {
        /* Widening a live sweep must not publish the shorter retained record. */
        configured=atomic_load(&configurations);
        request.duration_seconds=2*seconds;
        capture_pipeline_request(p,&request);
        for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
        assert(atomic_load(&configurations)>configured);
        rate=atomic_load(&sample_rate);
        blocks=(unsigned)ceil(request.duration_seconds*rate/128);
        completed=atomic_load(&releases);
        atomic_store(&available,blocks-1);
        expect_no_frame(p,completed+blocks-1);
        atomic_store(&available,1);assert(wait_frame(p,&frame));
        assert(frame.view_samples>=(size_t)ceil(request.duration_seconds*rate));
        assert(frame.view_samples<=(size_t)ceil(request.duration_seconds*rate)+1);
        for(unsigned pan=0;pan<2;++pan) {
            request.running=0;request.pan_fraction=pan;++request.force_sequence;
            capture_pipeline_request(p,&request);
            atomic_store(&available,1);assert(wait_frame(p,&frame));
            assert(frame.trigger_sample!=CAPTURE_NO_TRIGGER);
            for(unsigned ch=0;ch<2;++ch)for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)
                assert(frame.y[ch][x]!=INT16_MIN);
        }
    }
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
}
static void selftest_progress(unsigned hz,unsigned step,void *context)
{
    assert(context==&selftest_clocks && step==selftest_progress_count);
    assert(hz==(100000000u>>(6-step)));++selftest_progress_count;
}
static void startup_capture_test(void)
{
    char error[64]={0};
    selftest_mode=1;selftest_clocks=selftest_progress_count=0;
    assert(capture_selftest("/dev/test-adc",selftest_progress,&selftest_clocks,error,sizeof(error)));
    assert(selftest_clocks==7 && selftest_progress_count==7 && !*error && !atomic_load(&leased));
    selftest_clocks=selftest_progress_count=0;selftest_poll_fail_rate=100000000;
    assert(!capture_selftest("/dev/test-adc",selftest_progress,&selftest_clocks,error,sizeof(error)));
    assert(selftest_clocks==6 && strstr(error,"DMA ERROR / 50 MHz") && !atomic_load(&leased));
    selftest_poll_fail_rate=0;
    selftest_clocks=selftest_progress_count=0;selftest_fail_shift=2;
    assert(!capture_selftest("/dev/test-adc",selftest_progress,&selftest_clocks,error,sizeof(error)));
    assert(selftest_clocks==4 && strstr(error,"25 MHz") && !atomic_load(&leased));
    selftest_fail_shift=-1;selftest_clocks=selftest_progress_count=0;selftest_rail=3;
    assert(!capture_selftest("/dev/test-adc",selftest_progress,&selftest_clocks,error,sizeof(error)));
    assert(selftest_clocks==1 && strstr(error,"CH2-B AT 255") && strstr(error,"1.5625 MHz"));
    assert(!atomic_load(&leased));selftest_mode=0;selftest_rail=-1;
}

static void adc_level_test(void)
{
    const double gain[2]={0.004,0.004},zero[2]={128,128};
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    CaptureRequest request={0};
    CaptureMetrics metrics;
    static DisplayFrame frame;
    unsigned completed=atomic_load(&releases);
    assert(p);
    capture_pipeline_metrics(p,&metrics);
    assert(!metrics.adc_valid && metrics.adc_age_ms==UINT64_MAX);
    request.running=1;request.duration_seconds=1;request.record_samples=1000;
    request.pan_fraction=0.5;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SQUARE},1};
    atomic_store(&adc_pair,255u<<8);atomic_store(&poison_release,1);
    capture_pipeline_request(p,&request);atomic_store(&available,1);
    expect_no_frame(p,completed+1);
    capture_pipeline_metrics(p,&metrics);
    assert(metrics.adc_valid && metrics.adc_code[0]==0 && metrics.adc_code[1]==255);
    assert(metrics.adc_age_ms<1000 && !capture_pipeline_frame(p,&frame));
    /* A new real pair replaces the diagnostic value even with simulation off. */
    request.debug.enabled=0;
    atomic_store(&adc_pair,127u|(128u<<8));
    capture_pipeline_request(p,&request);atomic_store(&available,1);
    expect_no_frame(p,completed+2);
    capture_pipeline_metrics(p,&metrics);
    assert(metrics.adc_valid && metrics.adc_code[0]==127 && metrics.adc_code[1]==128);
    atomic_store(&fail_poll,1);
    for(unsigned i=0;i<100;++i){
        capture_pipeline_metrics(p,&metrics);
        if(metrics.source_error)break;
        scope_sleep_ms(5);
    }
    assert(metrics.source_error && !metrics.adc_valid);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&fail_poll,0);atomic_store(&poison_release,0);
    atomic_store(&adc_pair,42u|(42u<<8));
}

static void zero_correction_test(void)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};
    static DisplayFrame frame;
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    assert(p);
    request.running=1;request.duration_seconds=.2e-6;request.record_samples=128;
    request.pan_fraction=.5;request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);atomic_store(&available,1);assert(wait_frame(p,&frame));
    uint64_t sequence=frame.sequence;double mean=frame.mean[0];int y=frame.y[0][512];
    request.running=0;
    request.baseline[0]=(ChannelBaseline){1,-.3,.01,.004};
    request.baseline[1]=(ChannelBaseline){1,-.3,.01,.008}; /* Other relay stage: do not apply. */
    capture_pipeline_request(p,&request);assert(wait_frame(p,&frame));
    assert(frame.sequence==sequence && fabs(frame.mean[0]-(mean+.3))<1e-10);
    assert(fabs(frame.mean[1]-mean)<1e-10 && frame.y[0][512]==y-30);
    assert(fabs(frame.rms[0]-fabs(mean+.3))<1e-10);
    request.baseline[0].zero=0;
    capture_pipeline_request(p,&request);assert(wait_frame(p,&frame));
    assert(frame.sequence==sequence && frame.mean[0]==mean && frame.y[0][512]==y);
    assert(memory[64]==42); /* The source codes and nominal source zero are unchanged. */
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    memset(&request,0,sizeof(request));request.running=1;request.duration_seconds=.2e-6;
    request.record_samples=400;request.pan_fraction=.5;request.trigger_mode=1;request.trigger_voltage=.2;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SINE},1000000};
    request.baseline[0]=(ChannelBaseline){CHANNEL_ZERO_VALID,.4,0,.02,0};
    capture_pipeline_request(p,&request);atomic_store(&available,2);assert(wait_frame(p,&frame));
    assert(frame.trigger_sample!=CAPTURE_NO_TRIGGER && fabs(frame.trigger_x-512)<1e-6);
    assert(abs(frame.y[0][512]-180)<=2); /* Threshold is .2 V after the same .4 V zero correction. */
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
}

static void baseline_fresh_capture_test(void)
{
    double gain[2]={0.004,0.004},zero[2]={128,128};
    CaptureRequest request={0};
    static DisplayFrame frame;
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    assert(p);
    request.running=1;request.duration_seconds=0.2e-6;request.record_samples=128;
    request.pan_fraction=0.5;request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SINE},1000000};
    capture_pipeline_request(p,&request);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    for(unsigned ch=0;ch<2;++ch){
        uint64_t sequence=frame.sequence;
        request.baseline_mask=1U<<ch;
        request.debug.amplitude[ch]=1e-12;
        capture_pipeline_request(p,&request);
        scope_sleep_ms(50);
        assert(!capture_pipeline_frame(p,&frame)); /* No relabelled retained signal. */
        atomic_store(&available,1);assert(wait_frame(p,&frame));
        assert(frame.baseline_mask==(1U<<ch) && frame.sequence!=sequence);
        assert(fabs(frame.mean[ch])<.05 && frame.rms[ch]<.05);
    }
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
}

int main(void)
{
    ZynqCaptureSource source;
    CaptureBuffer a,b;
    double gain[2]={0.004,0.004},zero[2]={128,128};
    CapturePipeline *pipeline;
    CaptureRequest request={0};
    CaptureMetrics metrics;
    static DisplayFrame frame;
    uint64_t sequence;
    unsigned index;
    atomic_store(&bad_info,1);
    assert(!zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    atomic_store(&bad_info,0);
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_acquire(&source,&a,1)==0);
    atomic_store(&available,2);
    assert(zynq_capture_acquire(&source,&a,1)==1);index=source.index;
    assert(a.data==memory+64&&a.bytes==256);
    assert(zynq_capture_acquire(&source,&b,1)==1&&a.data!=b.data);
    assert(zynq_capture_release_index(&source,index));
    assert(!zynq_capture_release_index(&source,index));
    zynq_capture_close(&source);assert(!atomic_load(&leased));
    atomic_store(&packed_peaks,1);atomic_store(&available,1);
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_acquire(&source,&a,1)==1 && a.bytes==128 && a.peaks);
    for(unsigned pos=0;pos<a.bytes;pos+=4){
        assert(a.data[pos]==128 && a.data[pos+3]==131);
        assert(a.peaks[pos]==10 && a.peaks[pos+3]==250);
    }
    zynq_capture_close(&source);assert(!atomic_load(&leased));
    atomic_store(&packed_peaks,0);
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    atomic_store(&available,1);atomic_store(&bad_offset,1);
    assert(zynq_capture_acquire(&source,&a,1)==-1&&!atomic_load(&leased));
    atomic_store(&bad_offset,0);zynq_capture_close(&source);

    pipeline=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(pipeline);
    request.duration_seconds=1e-6;request.pan_fraction=0.5;request.running=1;
    request.record_samples=200; /* A complete visible interval fits in the fixture block. */
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},1000000};
    request.trigger_voltage=0.2;
    capture_pipeline_request(pipeline,&request);
    scope_sleep_ms(30);assert(!capture_pipeline_frame(pipeline,&frame));
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.maximum[0]>1.0&&frame.minimum[0]>=-0.020001);
    sequence=frame.sequence;
    request.running=0;request.pan_fraction=0.25;
    capture_pipeline_request(pipeline,&request);
    assert(wait_frame(pipeline,&frame)&&frame.sequence==sequence);
    assert(fabs(frame.trigger_x-768)<1e-6);
    request.overview_duration_seconds=2e-6;request.zoom_offset_seconds=0.1e-6;
    capture_pipeline_request(pipeline,&request);
    assert(wait_frame(pipeline,&frame)&&frame.sequence==sequence);
    assert(fabs(frame.trigger_x-921.6)<1e-6);
    assert(frame.overview_start==307&&frame.overview_end==819);
    request.pan_fraction=0.5;request.overview_duration_seconds=0;request.zoom_offset_seconds=0;
    request.running=0;request.duration_seconds=2e-6;request.debug.enabled=0;
    capture_pipeline_request(pipeline,&request);
    assert(wait_frame(pipeline,&frame)&&frame.sequence==sequence&&frame.maximum[0]>1);
    request.running=1;capture_pipeline_request(pipeline,&request);
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.sample_rate_hz==50000000&&frame.maximum[0]<0);
    request.trigger_mode=1;request.trigger_voltage=100;
    capture_pipeline_request(pipeline,&request);
    atomic_store(&available,1);scope_sleep_ms(30);
    capture_pipeline_metrics(pipeline,&metrics);
    assert(metrics.blocks==3&&metrics.waiting&&!capture_pipeline_frame(pipeline,&frame));
    request.running=0;++request.force_sequence;
    capture_pipeline_request(pipeline,&request);
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.trigger_sample!=CAPTURE_NO_TRIGGER);
    request.duration_seconds=0.2e-6;request.running=1;request.trigger_mode=2;request.trigger_voltage=0.2;request.debug.enabled=1;
    request.debug.frequency_hz=10000000;
    capture_pipeline_request(pipeline,&request);
    atomic_store(&available,2);assert(wait_frame(pipeline,&frame));
    scope_sleep_ms(30);
    assert(atomic_load(&available)==1&&!capture_pipeline_frame(pipeline,&frame));
    request.running=0;capture_pipeline_request(pipeline,&request);
    /* Rearming SINGLE requires a newly acquired interval. */
    atomic_store(&available,2);
    request.running=1;capture_pipeline_request(pipeline,&request);
    assert(wait_frame(pipeline,&frame)&&frame.trigger_sample!=CAPTURE_NO_TRIGGER);
    atomic_store(&available,0);
    request.trigger_mode=0;capture_pipeline_request(pipeline,&request);
    atomic_store(&fail_poll,1);scope_sleep_ms(40);
    capture_pipeline_metrics(pipeline,&metrics);assert(metrics.source_error&&metrics.blocks>=6);
    assert(!capture_pipeline_frame(pipeline,&frame));
    capture_pipeline_destroy(pipeline);assert(!atomic_load(&leased));
    atomic_store(&fail_poll,0);
    pipeline=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(pipeline);
    request.running=1;request.trigger_mode=1;request.force_sequence=0;
    request.duration_seconds=0.2e-6;request.debug.frequency_hz=10000000;
    capture_pipeline_request(pipeline,&request);atomic_store(&available,1);
    assert(wait_frame(pipeline,&frame)&&frame.view_samples==40);
    assert(frame.trigger_sample!=CAPTURE_NO_TRIGGER&&fabs(frame.trigger_x-512)<1e-6);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    capture_pipeline_destroy(pipeline);assert(!atomic_load(&leased));
    /* Long ROLL accumulates independent DMA blocks, then STOP reprojects
       the cached record without requiring the mapped memory to remain leased. */
    pipeline=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(pipeline);
    memset(&request,0,sizeof(request));
    request.running=1;request.duration_seconds=50;request.roll=1;
    request.record_samples=10000;request.pan_fraction=0.5;
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SINE},1};
    capture_pipeline_request(pipeline,&request);
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.sample_rate_hz==200 && frame.view_samples==128);
    assert(frame.y[0][0]==INT16_MIN && frame.y[0][SCOPE_PLOT_WIDTH-1]!=INT16_MIN);
    atomic_store(&available,80);
    for(unsigned i=0;i<100&&atomic_load(&available);++i)scope_sleep_ms(5);
    assert(!atomic_load(&available));
    scope_sleep_ms(50);
    while(capture_pipeline_frame(pipeline,&frame)){}
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.view_samples==10000&&frame.trigger_sample==CAPTURE_NO_TRIGGER);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    sequence=frame.sequence;
    request.running=0;request.duration_seconds=20;request.pan_fraction=0.3;
    capture_pipeline_request(pipeline,&request);assert(wait_frame(pipeline,&frame));
    assert(frame.sequence==sequence&&frame.view_samples==4000&&!atomic_load(&leased));
    request.running=1;request.duration_seconds=1e-7;request.roll=0;request.pan_fraction=0.5;
    capture_pipeline_request(pipeline,&request);
    for(unsigned i=0;i<100&&atomic_load(&sample_rate)!=200000000;++i)scope_sleep_ms(5);
    assert(atomic_load(&sample_rate)==200000000);
    while(capture_pipeline_frame(pipeline,&frame)){}
    atomic_store(&available,1);
    /* Reprojecting the retained STOP record can race with this delivery.
       Wait for the new DMA rate, not merely the first published frame. */
    {
        int got_rate=0;
        for(unsigned i=0;i<100;++i){
            if(capture_pipeline_frame(pipeline,&frame)&&frame.sample_rate_hz==200000000){got_rate=1;break;}
            scope_sleep_ms(5);
        }
        assert(got_rate&&frame.view_samples==20);
    }
    capture_pipeline_destroy(pipeline);assert(!atomic_load(&leased));
    {
        const double spans[]={0.1,0.2,0.5,1.0}; /* 10, 20, 50, 100 ms/div. */
        for(unsigned i=0;i<4;++i)complete_auto_frame_test(spans[i],0);
        complete_auto_frame_test(1.0,1);
        complete_force_frame_test();
    }
    adc_level_test();
    zero_correction_test();
    baseline_fresh_capture_test();
    startup_capture_test();
    return 0;
}
