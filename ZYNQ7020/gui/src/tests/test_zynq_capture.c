#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_zynq.h"
#include "capture_pipeline.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t memory[4 * 4096];
static atomic_uint leased, available, counter, releases;
static atomic_int fail_poll, bad_offset, bad_info;
static atomic_uint sample_rate = 200000000;
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
    if (atomic_load(&fail_poll)) { p->revents=POLLERR; return 1; }
    if (atomic_load(&available)) { p->revents=POLLIN; return 1; }
    scope_sleep_ms(timeout>5?5:timeout); return 0;
}
int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list args; void *arg;
    assert(fd==42); va_start(args,command); arg=va_arg(args,void *); va_end(args);
    if(command==OSC_CAPTURE_GET_INFO) {
        struct osc_capture_info info={OSC_CAPTURE_ABI,3,OSC_CAPTURE_SAMPLE_FORMAT,1,4,4096,sizeof(memory)};
        if(atomic_load(&bad_info))info.buffers=1000;
        *(struct osc_capture_info *)arg=info;
    } else if(command==OSC_CAPTURE_CONFIGURE) {
        struct osc_capture_config *config=arg;
        assert(!atomic_load(&leased));
        sample_rate=osc_capture_rate(config->rate_index); assert(sample_rate);
        atomic_store(&counter,0);
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
    } else if(command==OSC_CAPTURE_RELEASE) {
        unsigned index=*(uint32_t *)arg;
        assert(index<4&&(atomic_fetch_and(&leased,~(1u<<index))&(1u<<index)));
        atomic_fetch_add(&releases,1);
    } else assert(0);
    return 0;
}
static int wait_frame(CapturePipeline *p, DisplayFrame *f)
{
    unsigned i;
    for(i=0;i<100;++i){if(capture_pipeline_frame(p,f))return 1;scope_sleep_ms(5);}
    return 0;
}
int main(void)
{
    ZynqCaptureSource source;
    CaptureBuffer a,b;
    double gain[2]={0.004,0.004},zero[2]={128,128};
    CapturePipeline *pipeline;
    CaptureRequest request={0};
    CaptureMetrics metrics;
    DisplayFrame frame;
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
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    atomic_store(&available,1);atomic_store(&bad_offset,1);
    assert(zynq_capture_acquire(&source,&a,1)==-1&&!atomic_load(&leased));
    atomic_store(&bad_offset,0);zynq_capture_close(&source);

    pipeline=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(pipeline);
    request.duration_seconds=1e-6;request.pan_fraction=0.5;request.running=1;
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},1000000};
    request.trigger_voltage=0.2;
    capture_pipeline_request(pipeline,&request);
    scope_sleep_ms(30);assert(!capture_pipeline_frame(pipeline,&frame));
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.maximum[0]>1.0&&frame.minimum[0]>=0);
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
    assert(frame.sequence>sequence&&frame.maximum[0]<0);
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
    capture_pipeline_request(pipeline,&request);
    atomic_store(&available,2);assert(wait_frame(pipeline,&frame));
    scope_sleep_ms(30);
    assert(atomic_load(&available)==1&&!capture_pipeline_frame(pipeline,&frame));
    request.running=0;capture_pipeline_request(pipeline,&request);
    /* A 128-sample block can miss the rising edge of the 200-sample period. */
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
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.sample_rate_hz==200000000&&frame.view_samples==20);
    capture_pipeline_destroy(pipeline);assert(!atomic_load(&leased));
    return 0;
}
