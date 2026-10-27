#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_zynq.h"
#include "capture_pipeline.h"
#include "capture_selftest.h"
#include "capture_settings.h"
#include "analysis_uapi.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum { FIXTURE_BLOCK_BYTES = 2 * 1024 * 1024 };
static uint8_t memory[8 * FIXTURE_BLOCK_BYTES];
static atomic_uint leased, available, counter, releases, configurations;
static atomic_int fail_poll, bad_offset, bad_info;
static atomic_int analysis_present,analysis_jobs;
static atomic_int slow_bulk_decode;
static atomic_uint sample_rate = 200000000;
static atomic_uint adc_pair = 42 | (42 << 8);
static atomic_int poison_release;
static atomic_int packed_peaks;
static atomic_int hardware_bounds;
static atomic_int summary_gap;
static atomic_int bounds_fault;
static atomic_int hardware_window_bounds,burst_jitter;
static atomic_int supports_probe;
static atomic_uint probe_word;
static atomic_int supports_dsp,dsp_fixture;
static struct osc_capture_dsp dsp_configuration;
static uint64_t dsp_fixture_sample=CAPTURE_NO_TRIGGER;
static uint64_t dsp_last_sample=CAPTURE_NO_TRIGGER;
static struct {uint64_t stats[2],sequence;uint32_t raw,probe,probe_sides;} window_cache[2];
static uint32_t previous_bounds,previous_bytes,previous_rate;
static uint64_t previous_sequence;
static int bounds_valid;
static atomic_uint burst_samples,burst_width=32;
static atomic_uint burst_period;
static atomic_int burst_invert;
static atomic_uint burst_channel;
static atomic_uint_fast64_t burst_at;
static atomic_int pause_processing,processing_entered;
static atomic_uint processed_samples;
static atomic_uint processed_count;
static unsigned fixture_buffers(void){return atomic_load(&supports_dsp)?8u:4u;}
static uint64_t mock_dsp_event(void)
{
    uint64_t minimum=dsp_configuration.minimum_sample,start=atomic_load(&burst_at),first=CAPTURE_NO_TRIGGER;
    unsigned period=atomic_load(&burst_period),width=atomic_load(&burst_width);
    unsigned edge=(dsp_configuration.control>>OSC_CAPTURE_DSP_EDGE_SHIFT)&3;
    if(dsp_last_sample!=CAPTURE_NO_TRIGGER && minimum<dsp_last_sample+dsp_configuration.holdoff_samples+1)
        minimum=dsp_last_sample+dsp_configuration.holdoff_samples+1;
    for(unsigned direction=0;direction<2;++direction)if(edge==2 || edge==direction){
        uint64_t at=start+(direction?(period?period/2:width):0);
        if(at<minimum && period)at+=((minimum-at+period-1)/period)*period;
        if(at>=minimum && (period?at<start+width:at<=start+width) && at<first)first=at;
    }
    return first;
}
int __real_capture_find_trigger_filtered(const CaptureBuffer *,unsigned,unsigned,double,unsigned,unsigned,double,uint64_t,uint64_t *);
int __wrap_capture_find_trigger_filtered(const CaptureBuffer *b,unsigned ch,unsigned edge,double volts,unsigned hysteresis,
    unsigned filter,double noise,uint64_t first,uint64_t *sample)
{
    assert(atomic_load(&dsp_fixture)!=2); /* Hardware mode must not scan the signal on ARM. */
    return __real_capture_find_trigger_filtered(b,ch,edge,volts,hysteresis,filter,noise,first,sample);
}
static unsigned burst_code(uint64_t sample)
{
    uint64_t at=atomic_load(&burst_at);unsigned period=atomic_load(&burst_period);
    int pulse=sample>=at && sample-at<atomic_load(&burst_width);
    if(pulse && period)pulse=(sample-at)%period<period/2;
    int value=(pulse!=!!atomic_load(&burst_invert))?180:100;
    int jitter=atomic_load(&burst_jitter);return (unsigned)(value+(sample%2?jitter:-jitter));
}
static unsigned probe_reference(unsigned control,unsigned raw,unsigned sum,unsigned variance_scaled)
{
    if(!(control&OSC_CAPTURE_PROBE_ENABLE))return 0;
    unsigned code=(control>>8)&255,exponent=code>>5,mantissa=code&31;
    double mean=sum/17.0,variance=variance_scaled/289.0,low=raw,high=raw;
    if(code==255)low=high=mean;
    else if(code){
        double power=exponent?(32+mantissa)<<(exponent-1):mantissa;
        double limit=exponent?(33+mantissa)<<(exponent-1):mantissa+1;
        low=INFINITY;high=-INFINITY;
        for(unsigned i=0;i<2;++i){
            double n=i?limit:power;
            double value=variance>n?mean+(1-n/variance)*(raw-mean):mean;
            if(value<low)low=value;if(value>high)high=value;
        }
        if(variance>=9*power){if(raw<low)low=raw;if(raw>high)high=raw;}
    }
    return (low<=(int)(control&255)-1?1u:0u)|(high>=(control&255)+1?2u:0u);
}
static uint64_t window_stats(uint64_t first,unsigned samples,unsigned control,unsigned *sides)
{
    unsigned jitter=atomic_load(&burst_jitter),base=atomic_load(&burst_invert)?180:100;
    unsigned minimum=17*base-jitter,maximum=17*base+jitter,variance=288*jitter*jitter;
    *sides=(control&(1u<<16))?probe_reference(control,100,1700,0):
        probe_reference(control,base-jitter,17*base-jitter,variance)|
        probe_reference(control,base+jitter,17*base+jitter,variance);
    uint64_t at=atomic_load(&burst_at),begin=at>8?at-8:0,end=at+atomic_load(&burst_width)+8;
    if(begin<first)begin=first;if(end>first+samples)end=first+samples;
    for(uint64_t i=begin;i<end;++i){
        unsigned sum=0,squares=0;
        for(int k=-8;k<=8;++k){unsigned value=burst_code(i+k);sum+=value;squares+=value*value;}
        unsigned v=17*squares-sum*sum;
        if(!(control&(1u<<16)))*sides|=probe_reference(control,burst_code(i),sum,v);
        if(sum<minimum)minimum=sum;if(sum>maximum)maximum=sum;if(v>variance)variance=v;
    }
    return minimum|((uint64_t)maximum<<13)|((uint64_t)variance<<26);
}
static int selftest_mode, selftest_fail_shift=-1, selftest_rail=-1;
static unsigned selftest_clocks, selftest_progress_count;
static unsigned selftest_poll_fail_rate;
int __real_capture_process(const CaptureBuffer *,const CaptureView *,DisplayFrame *);
int __real_zynq_capture_decode_packet(ZynqCaptureSource *,const ZynqCapturePacket *,size_t,size_t,
    CaptureBuffer *,uint8_t *,uint8_t *,size_t);
int __wrap_zynq_capture_decode_packet(ZynqCaptureSource *s,const ZynqCapturePacket *packet,size_t first,size_t samples,
    CaptureBuffer *buffer,uint8_t *data,uint8_t *peaks,size_t capacity)
{
    int result=__real_zynq_capture_decode_packet(s,packet,first,samples,buffer,data,peaks,capacity);
    /* Model packets arriving during expensive bulk decoding. Already leased
       neighbours stay intact; a subsequent dequeue observes a sequence gap. */
    if(atomic_load(&slow_bulk_decode) && samples>2)atomic_fetch_add(&counter,3);
    return result;
}
int __wrap_capture_process(const CaptureBuffer *b,const CaptureView *v,DisplayFrame *f)
{
    atomic_store(&processed_samples,(unsigned)(b->bytes/2));
    atomic_fetch_add(&processed_count,1);
    if(atomic_load(&pause_processing)){
        atomic_store(&processing_entered,1);
        while(atomic_load(&pause_processing))scope_sleep_ms(1);
    }
    return __real_capture_process(b,v,f);
}
int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    if(!strcmp(path,"/dev/oscill-analysis")){
        if(atomic_load(&analysis_present))return 43;
        errno=ENODEV;return -1;
    }
    assert(!strcmp(path,"/dev/test-adc"));return 42;
}
int __wrap_close(int fd) { if(fd==43)return 0;assert(fd==42); atomic_store(&leased,0); return 0; }
void *__wrap_mmap(void *addr, size_t length, int protection, int flags, int fd, long offset)
{
    (void)addr; assert(length==fixture_buffers()*FIXTURE_BLOCK_BYTES&&protection==PROT_READ&&flags==MAP_SHARED&&fd==42&&!offset);
    return memory;
}
int __wrap_munmap(void *addr, size_t length) { assert(addr==memory&&length==fixture_buffers()*FIXTURE_BLOCK_BYTES); return 0; }
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
    va_start(args,command); arg=va_arg(args,void *); va_end(args);
    if(fd==43){
        if(command==OSC_ANALYSIS_GET_INFO){
            *(struct osc_analysis_info *)arg=(struct osc_analysis_info){OSC_ANALYSIS_ABI,OSC_ANALYSIS_MAX_SAMPLES,
                OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY|OSC_ANALYSIS_ASYNC,0};return 0;
        }
        assert(command==OSC_ANALYSIS_BEGIN);atomic_fetch_add(&analysis_jobs,1);errno=EIO;return -1;
    }
    assert(fd==42);
    if(command==OSC_CAPTURE_GET_INFO) {
        struct osc_capture_info info={OSC_CAPTURE_ABI,OSC_CAPTURE_REGISTER_VERSION,OSC_CAPTURE_SAMPLE_FORMAT,1,fixture_buffers(),FIXTURE_BLOCK_BYTES,fixture_buffers()*FIXTURE_BLOCK_BYTES};
        if(atomic_load(&bad_info))info.buffers=1000;
        *(struct osc_capture_info *)arg=info;
    } else if(command==OSC_CAPTURE_CONFIGURE) {
        struct osc_capture_config *config=arg;
        assert(!atomic_load(&leased));
        sample_rate=osc_capture_rate(config->rate_index); assert(sample_rate);
        atomic_store(&counter,0);
        bounds_valid=0;
        atomic_fetch_add(&configurations,1);
        if(atomic_load(&dsp_fixture)==2)dsp_fixture_sample=dsp_last_sample=CAPTURE_NO_TRIGGER;
    } else if(command==OSC_CAPTURE_TEST_CLOCK) {
        const struct osc_capture_test_clock *config=arg;
        assert(!atomic_load(&leased) && config->shift<=6 && config->payload_shift==17);
        assert(config->shift==6-selftest_clocks);
        if((int)config->shift==selftest_fail_shift){errno=EIO;return -1;}
        sample_rate=200000000u>>config->shift;
        atomic_store(&counter,0);atomic_store(&available,4);++selftest_clocks;
    } else if(command==OSC_CAPTURE_SET_PROBE){
        if(!atomic_load(&supports_probe)){errno=ENOTTY;return -1;}
        const struct osc_capture_probe *probe=arg;
        assert(!atomic_load(&leased) && !(probe->control&~OSC_CAPTURE_PROBE_MASK));
        atomic_store(&probe_word,probe->control);
    } else if(command==OSC_CAPTURE_SET_DSP){
        if(!atomic_load(&supports_dsp)){errno=ENOTTY;return -1;}
        dsp_configuration=*(const struct osc_capture_dsp *)arg;
        if(atomic_load(&dsp_fixture)==2)dsp_fixture_sample=CAPTURE_NO_TRIGGER;
    } else if(command==OSC_CAPTURE_DEQUEUE) {
        struct osc_capture_block *b=arg;
        unsigned i,mask=atomic_load(&leased),sequence;
        if(!atomic_load(&available)){errno=EAGAIN;return -1;}
        for(i=0;i<fixture_buffers()&&(mask&(1u<<i));++i){}
        assert(i<fixture_buffers()); atomic_fetch_or(&leased,1u<<i); atomic_fetch_sub(&available,1);
        sequence=atomic_fetch_add(&counter,1);
        if(sequence==2 && atomic_load(&summary_gap)){sequence+=10;atomic_fetch_add(&counter,10);}
        memset(memory+i*FIXTURE_BLOCK_BYTES,0,64);
        memset(b,0,sizeof(*b)); b->index=i;b->bytes=256;b->data_offset=atomic_load(&bad_offset)?FIXTURE_BLOCK_BYTES-6:64;
        b->sequence=sequence;b->sample_rate_hz=sample_rate;b->first_sample=(uint64_t)sequence*128;
        b->trigger_sample=CAPTURE_NO_TRIGGER;b->hardware_ns=b->first_sample*5;
        memset(memory+i*FIXTURE_BLOCK_BYTES+64,42,256);
        memory[i*FIXTURE_BLOCK_BYTES+64+254]=(uint8_t)atomic_load(&adc_pair);
        memory[i*FIXTURE_BLOCK_BYTES+64+255]=(uint8_t)(atomic_load(&adc_pair)>>8);
        if(selftest_mode && selftest_rail>=0)
            for(unsigned pos=(unsigned)selftest_rail;pos<256;pos+=4)memory[i*FIXTURE_BLOCK_BYTES+64+pos]=255;
        if(atomic_load(&packed_peaks)){
            uint8_t *data=memory+i*FIXTURE_BLOCK_BYTES+64;
            b->flags=OSC_CAPTURE_PEAKS;b->first_sample=(uint64_t)sequence*64;
            for(unsigned pos=0;pos<256;pos+=8){
                const uint8_t group[]={128,129,130,131,10,20,240,250};
                memcpy(data+pos,group,8);
            }
        }
        if(atomic_load(&burst_samples)){
            unsigned samples=atomic_load(&burst_samples);
            uint8_t *data=memory+i*FIXTURE_BLOCK_BYTES+64;
            int peaks=atomic_load(&packed_peaks);
            b->bytes=samples*(peaks?4:2);b->first_sample=(uint64_t)sequence*samples;
            b->flags=peaks?OSC_CAPTURE_PEAKS:0;
            memset(data,100,b->bytes);
            uint64_t at=atomic_load(&burst_at);
            for(unsigned pos=0;pos<samples;++pos){
                unsigned target=peaks?(pos/2)*8+(pos%2)*2:pos*2;
                data[target+atomic_load(&burst_channel)]=(uint8_t)burst_code(b->first_sample+pos);
            }
            if(peaks)for(unsigned pos=0;pos<samples;pos+=2){
                unsigned target=(pos/2)*8;
                for(unsigned ch=0;ch<2;++ch){
                    data[target+4+ch]=data[target+ch]<data[target+2+ch]?data[target+ch]:data[target+2+ch];
                    data[target+6+ch]=data[target+ch]>data[target+2+ch]?data[target+ch]:data[target+2+ch];
                }
            }
        }
        if(atomic_load(&hardware_bounds)){
            uint32_t *header=(uint32_t *)(memory+i*FIXTURE_BLOCK_BYTES);
            if(bounds_valid && sequence==previous_sequence+1){
                header[10]=(uint32_t)previous_sequence;header[11]=(uint32_t)(previous_sequence>>32);
                header[12]=previous_bounds;header[13]=0x31584d4d;
                header[14]=previous_bytes;header[15]=previous_rate;
                switch(atomic_load(&bounds_fault)){
                    case 1:header[13]^=1;break;
                    case 2:header[10]-=2;break;
                    case 3:header[14]-=8;break;
                    case 4:header[15]/=2;break;
                    case 5:header[12]&=0xff00ffff;break;
                }
            }
            unsigned lo[2]={255,255},hi[2]={0,0},peak=!!(b->flags&OSC_CAPTURE_PEAKS);
            unsigned count=b->bytes/(peak?4:2);const uint8_t *data=memory+i*FIXTURE_BLOCK_BYTES+64;
            for(unsigned pos=0;pos<count;++pos)for(unsigned ch=0;ch<2;++ch){
                unsigned value=data[(peak?(pos/2)*8+(pos%2)*2:pos*2)+ch];
                if(value<lo[ch])lo[ch]=value;if(value>hi[ch])hi[ch]=value;
            }
            previous_bounds=lo[0]|(lo[1]<<8)|(hi[0]<<16)|(hi[1]<<24);
            previous_bytes=b->bytes;previous_rate=b->sample_rate_hz;previous_sequence=sequence;bounds_valid=1;
            if(atomic_load(&hardware_window_bounds)){
                unsigned slot=sequence%2;
                if(sequence>=3 && window_cache[slot].sequence==sequence-2){
                    header[9]=window_cache[slot].raw;
                    memcpy(header+10,window_cache[slot].stats,16);
                    header[14]=0x32584d4d;header[15]=sequence-2;
                    if(window_cache[slot].probe&OSC_CAPTURE_PROBE_ENABLE){
                        header[11]|=(window_cache[slot].probe&32767)<<17;
                        header[13]|=((window_cache[slot].probe>>15)|(window_cache[slot].probe_sides<<3))<<17;
                        header[14]=0x33584d4d;
                    }
                    switch(atomic_load(&bounds_fault)){
                        case 1:header[14]^=1;break;
                        case 2:header[15]-=1;break;
                        case 3:header[11]|=0x80000000;break;
                        case 4:header[10]|=8191;break;
                        case 5:header[11]|=0x1ffff;break;
                    }
                }
                window_cache[slot].sequence=sequence;window_cache[slot].raw=previous_bounds;
                window_cache[slot].probe=atomic_load(&probe_word);
                window_cache[slot].stats[0]=window_stats(b->first_sample,count,window_cache[slot].probe,&window_cache[slot].probe_sides);
                window_cache[slot].stats[1]=1700|((uint64_t)1700<<13);
            }
        }
        if(atomic_load(&dsp_fixture) && (dsp_configuration.control&OSC_CAPTURE_DSP_ENABLE)){
            if(atomic_load(&dsp_fixture)==2 && dsp_fixture_sample==CAPTURE_NO_TRIGGER)dsp_fixture_sample=mock_dsp_event();
            uint32_t *header=(uint32_t *)(memory+b->index*FIXTURE_BLOCK_BYTES);
            uint64_t event=dsp_fixture_sample;
            if(atomic_load(&dsp_fixture)==2 && event>=b->first_sample)event=CAPTURE_NO_TRIGGER;
            if(atomic_load(&dsp_fixture)==2 && event!=CAPTURE_NO_TRIGGER)dsp_last_sample=event;
            header[9]=dsp_configuration.arm_id;header[10]=(uint32_t)event;
            header[11]=(uint32_t)(event>>32);header[12]=dsp_configuration.control;
            header[13]=0;header[14]=0x31475254;header[15]=event!=CAPTURE_NO_TRIGGER;
            switch(atomic_load(&bounds_fault)){
                case 1:header[14]^=1;break;
                case 2:header[12]|=1u<<31;break;
                case 3:header[13]=1;break;
                case 4:header[15]=2;break;
                case 5:header[10]=header[11]=UINT32_MAX;break;
                case 6:header[15]=0;break;
            }
        }
    } else if(command==OSC_CAPTURE_RELEASE) {
        unsigned index=*(uint32_t *)arg;
        assert(index<fixture_buffers()&&(atomic_fetch_and(&leased,~(1u<<index))&(1u<<index)));
        atomic_fetch_add(&releases,1);
        if(atomic_load(&poison_release))memset(memory+index*FIXTURE_BLOCK_BYTES+64,238,256);
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

static void trigger_filter_support_test(void)
{
    const double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};static DisplayFrame frame;
    unsigned completed=atomic_load(&releases);
    unsigned samples=4u<<capture_payload_shift_for_depth(200000000,100000);
    atomic_store(&burst_samples,samples);atomic_store(&burst_width,16);
    atomic_store(&burst_at,samples-20000-16);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    request.running=1;request.duration_seconds=.0002;request.record_samples=100000;
    request.pan_fraction=.5;request.trigger_mode=1;request.filter_level[0]=3;
    request.zero_y[0]=request.zero_y[1]=240;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);atomic_store(&available,1);
    /* Sixteen high samples cannot cross code 128 through a 65-sample average.
       Cutting off the available posttrigger data would invent a boundary edge. */
    expect_no_frame(p,completed+1);
    atomic_store(&burst_at,samples+30000);atomic_store(&burst_width,128);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    assert(frame.trigger_sample>samples+30000-32 && frame.trigger_sample<samples+30000+32);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&burst_samples,0);atomic_store(&burst_width,32);
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

static void analysis_failure_test(void)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};CaptureMetrics metrics={0};static DisplayFrame frame;
    atomic_store(&analysis_present,1);atomic_store(&analysis_jobs,0);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    request.running=1;request.duration_seconds=.2e-6;request.record_samples=128;
    request.pan_fraction=.5;request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);atomic_store(&available,1);
    for(unsigned i=0;i<200;++i){capture_pipeline_metrics(p,&metrics);if(metrics.source_error)break;scope_sleep_ms(5);}
    assert(metrics.source_error && !metrics.waveforms && atomic_load(&analysis_jobs)==1);
    assert(!capture_pipeline_frame(p,&frame));
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));atomic_store(&analysis_present,0);
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
    request.baseline[0][0]=(ChannelBaseline){1,-.3,.01,.004};
    request.baseline[1][0]=(ChannelBaseline){1,-.3,.01,.008}; /* Other relay stage: do not apply. */
    capture_pipeline_request(p,&request);assert(wait_frame(p,&frame));
    assert(frame.sequence==sequence && fabs(frame.mean[0]-(mean+.3))<1e-10);
    assert(fabs(frame.mean[1]-mean)<1e-10 && frame.y[0][512]==y-30);
    assert(fabs(frame.rms[0]-fabs(mean+.3))<1e-10);
    request.baseline[0][0].zero=0;
    capture_pipeline_request(p,&request);assert(wait_frame(p,&frame));
    assert(frame.sequence==sequence && frame.mean[0]==mean && frame.y[0][512]==y);
    assert(memory[64]==42); /* The source codes and nominal source zero are unchanged. */
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    memset(&request,0,sizeof(request));request.running=1;request.duration_seconds=.2e-6;
    request.record_samples=400;request.pan_fraction=.5;request.trigger_mode=1;request.trigger_voltage=.2;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SINE},1000000};
    request.baseline[0][0]=(ChannelBaseline){CHANNEL_ZERO_VALID,.4,0,.02,0};
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

static void sparse_burst_test(unsigned depth, unsigned offset, int mode, double pan)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};
    static DisplayFrame frame;
    unsigned rate=osc_capture_rate(capture_rate_index(.001,depth,0));
    unsigned samples=(rate==200000000?4u:2u)<<capture_payload_shift_for_depth(rate,depth);
    uint64_t trigger=(uint64_t)samples*2+offset;
    unsigned completed=atomic_load(&releases),configured=atomic_load(&configurations);
    atomic_store(&burst_samples,samples);atomic_store(&burst_at,trigger);
    atomic_store(&packed_peaks,rate!=200000000);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    assert(p);
    request.running=1;request.duration_seconds=.001;request.record_samples=depth;
    request.pan_fraction=pan;request.trigger_mode=mode;
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    assert(atomic_load(&configurations)>configured);
    atomic_store(&available,2);
    for(unsigned i=0;i<100&&atomic_load(&releases)<completed+2;++i)scope_sleep_ms(5);
    assert(atomic_load(&releases)>=completed+2);
    while(capture_pipeline_frame(p,&frame)){}
    atomic_store(&available,1);
    if(offset+ceil(rate*.001*pan)>=samples){
        /* The pulse needs posttrigger samples from the following block. */
        for(unsigned i=0;i<100&&atomic_load(&releases)<completed+3;++i)scope_sleep_ms(5);
        assert(atomic_load(&releases)>=completed+3);
        atomic_store(&available,1);
    }
    int found=0;
    for(unsigned i=0;i<100;++i){
        if(capture_pipeline_frame(p,&frame) && frame.trigger_sample==trigger){found=1;break;}
        scope_sleep_ms(5);
    }
    if(!found || fabs(frame.trigger_x-(1-pan)*SCOPE_PLOT_WIDTH)>=1e-6)
        fprintf(stderr,"burst depth=%u offset=%u mode=%d pan=%g expected=%llu actual=%llu x=%g found=%d\n",
            depth,offset,mode,pan,(unsigned long long)trigger,(unsigned long long)frame.trigger_sample,frame.trigger_x,found);
    assert(found && fabs(frame.trigger_x-(1-pan)*SCOPE_PLOT_WIDTH)<1e-6);
    assert(frame.peak_detect==(rate!=200000000));
    assert(frame.view_samples>=rate/1000 && frame.view_samples<=rate/1000+1);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&available,0);atomic_store(&burst_samples,0);atomic_store(&packed_peaks,0);
}

static void processing_overlap_test(void)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};CaptureMetrics metrics;
    static DisplayFrame frame;
    unsigned completed=atomic_load(&releases);
    atomic_store(&pause_processing,1);atomic_store(&processing_entered,0);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);
    assert(p);
    request.running=1;request.duration_seconds=.2e-6;request.record_samples=128;
    request.pan_fraction=.5;request.trigger_mode=1;request.trigger_voltage=.2;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    request.debug=(CaptureDebug){1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},10000000};
    capture_pipeline_request(p,&request);atomic_store(&available,1);
    for(unsigned i=0;i<100&&!atomic_load(&processing_entered);++i)scope_sleep_ms(5);
    assert(atomic_load(&processing_entered));
    atomic_store(&available,8);
    for(unsigned i=0;i<100&&atomic_load(&releases)<completed+9;++i)scope_sleep_ms(5);
    assert(atomic_load(&releases)>=completed+9);
    capture_pipeline_metrics(p,&metrics);
    assert(metrics.blocks>=9 && metrics.overwritten && !capture_pipeline_frame(p,&frame));
    assert(!atomic_load(&leased)); /* A busy processor must not retain DMA leases. */
    atomic_store(&pause_processing,0);
    int latest=0;
    for(unsigned i=0;i<100;++i){
        if(capture_pipeline_frame(p,&frame)&&frame.sequence>=7){latest=1;break;}
        scope_sleep_ms(5);
    }
    assert(latest && frame.maximum[0]>1);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
}

static void hardware_bounds_burst_test(unsigned offset,int edge,int mode,int invert,int windows)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};CaptureMetrics metrics;
    static DisplayFrame frame;
    const unsigned samples=2u<<capture_payload_shift_for_depth(50000000,100000);
    uint64_t trigger=(uint64_t)samples*80+offset;
    unsigned configured=atomic_load(&configurations);
    atomic_store(&hardware_bounds,1);atomic_store(&burst_samples,samples);
    atomic_store(&hardware_window_bounds,windows);atomic_store(&burst_jitter,windows?40:0);
    atomic_store(&supports_probe,windows==2);
    int gap=offset==0 && edge==0 && mode==1;atomic_store(&summary_gap,gap);
    atomic_store(&burst_invert,invert);
    atomic_store(&packed_peaks,1);atomic_store(&burst_at,trigger);
    atomic_store(&burst_width,4096);atomic_store(&burst_period,512);
    atomic_store(&processed_count,0);atomic_store(&poison_release,1);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    request.running=1;request.duration_seconds=.001;request.record_samples=100000;
    request.pan_fraction=.5;request.trigger_mode=mode;request.trigger_edge=edge;
    request.filter_level[0]=2;
    if(windows)request.baseline[0][0]=(ChannelBaseline){CHANNEL_NOISE_VALID,0,.2,.004,.004};
    request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    assert(atomic_load(&configurations)>configured);
    atomic_store(&available,86);
    int found=0;
    /* The mock generates and checks >20 million codes on the acquisition
       thread; sanitizer instrumentation is not a hardware DMA rate test. */
    for(unsigned i=0;i<1000;++i){if(capture_pipeline_frame(p,&frame)){found=1;break;}scope_sleep_ms(5);}
    assert(found);
    if(edge!=2 && invert!=(edge==1))trigger+=256;
    assert(frame.trigger_sample>=trigger-40 && frame.trigger_sample<=trigger+40);
    assert(atomic_load(&processed_samples)==100000 && frame.peak_detect);
    capture_pipeline_metrics(p,&metrics);
    assert(metrics.blocks>=65 && metrics.rejected_blocks>=60 && !metrics.source_error);
    if(windows==2)assert(metrics.probe_blocks>=60);
    assert(gap?metrics.dropped>=10:!metrics.dropped);
    if(mode==1){
        for(unsigned i=0;i<100&&atomic_load(&available);++i)scope_sleep_ms(5);
        assert(!atomic_load(&available));scope_sleep_ms(30);
        assert(atomic_load(&processed_count)==1); /* No later edges of the consumed burst. */
    }
    request.running=0;capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&leased);++i)scope_sleep_ms(5);
    assert(!atomic_load(&leased));
    capture_pipeline_destroy(p);
    atomic_store(&available,0);atomic_store(&hardware_bounds,0);atomic_store(&burst_samples,0);
    atomic_store(&packed_peaks,0);atomic_store(&poison_release,0);atomic_store(&burst_width,32);
    atomic_store(&burst_invert,0);
    atomic_store(&summary_gap,0);
    atomic_store(&burst_period,0);
    atomic_store(&hardware_window_bounds,0);atomic_store(&burst_jitter,0);
    atomic_store(&supports_probe,0);atomic_store(&probe_word,0);
}

static void encoded_packet_test(void)
{
    ZynqCaptureSource source;ZynqCapturePacket first,second;
    CaptureBuffer decoded;
    double gain[2]={.004,.004},zero[2]={128,128};
    uint8_t data[8]={0},peaks[8]={0};
    atomic_store(&hardware_bounds,1);atomic_store(&packed_peaks,1);
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_configure(&source,1,17));atomic_store(&available,2);
    assert(zynq_capture_dequeue(&source,&first,1)==1 && !first.bounds_valid);
    assert(!first.metadata.data && first.metadata.bytes==128);
    assert(zynq_capture_dequeue(&source,&second,1)==1 && second.bounds_valid);
    assert(second.bounds_sequence==first.metadata.sequence);
    assert(second.minimum[0]==128 && second.maximum[0]==130);
    assert(second.minimum[1]==129 && second.maximum[1]==131);
    assert(!zynq_capture_decode_packet(&source,&first,1,2,&decoded,data,peaks,sizeof(data)));
    assert(!zynq_capture_decode_packet(&source,&first,2,3,&decoded,data,peaks,sizeof(data)));
    assert(zynq_capture_decode_packet(&source,&first,2,2,&decoded,data,peaks,sizeof(data)));
    assert(decoded.first_sample==2 && decoded.bytes==4 && decoded.data==data && decoded.peaks==peaks);
    assert(data[0]==128 && data[2]==130 && peaks[0]==10 && peaks[2]==240 && data[4]==0 && peaks[4]==0);
    assert(zynq_capture_release_index(&source,first.index));
    assert(!zynq_capture_decode_packet(&source,&first,0,2,&decoded,data,peaks,sizeof(data)));
    assert(zynq_capture_release_index(&source,second.index));
    for(int fault=1;fault<=5;++fault){
        atomic_store(&bounds_fault,fault);atomic_store(&available,1);
        assert(zynq_capture_dequeue(&source,&second,1)==1 && !second.bounds_valid);
        assert(zynq_capture_release_index(&source,second.index));
    }
    atomic_store(&bounds_fault,0);zynq_capture_close(&source);
    atomic_store(&hardware_bounds,0);atomic_store(&packed_peaks,0);
}

static void continuous_bounds_test(unsigned depth,int edge)
{
    double gain[2]={.004,.004},zero[2]={128,128};
    CaptureRequest request={0};CaptureMetrics metrics;
    static DisplayFrame frame;
    unsigned rate=osc_capture_rate(capture_rate_index(.001,depth,0));
    unsigned samples=(rate==200000000?4u:2u)<<capture_payload_shift_for_depth(rate,depth);
    unsigned configured=atomic_load(&configurations);
    atomic_store(&hardware_bounds,1);atomic_store(&burst_samples,samples);
    atomic_store(&packed_peaks,rate!=200000000);atomic_store(&burst_at,0);
    atomic_store(&burst_width,UINT32_MAX);atomic_store(&burst_period,512);
    atomic_store(&processed_count,0);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    request.running=1;request.duration_seconds=.001;request.record_samples=depth;
    request.pan_fraction=.5;request.trigger_mode=1;request.trigger_edge=edge;
    request.filter_level[0]=2;request.zero_y[0]=request.zero_y[1]=200;
    request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    assert(atomic_load(&configurations)>configured);
    atomic_store(&available,1);assert(wait_frame(p,&frame));
    uint64_t first=(uint64_t)ceil(rate*.001*.5),phase=edge==1?256:0;
    uint64_t expected=((first-phase+511)/512)*512+phase;
    if(edge==2)expected=((first+255)/256)*256;
    if(frame.trigger_sample<expected-8 || frame.trigger_sample>expected+8)
        fprintf(stderr,"continuous depth=%u edge=%d expected=%llu actual=%llu\n",depth,edge,
            (unsigned long long)expected,(unsigned long long)frame.trigger_sample);
    assert(frame.trigger_sample>=expected-8 && frame.trigger_sample<=expected+8);
    uint64_t previous=frame.trigger_sample;
    for(unsigned i=0;i<4;++i){
        /* Header lookahead consumes one additional block on the fast path. */
        atomic_store(&available,2);assert(wait_frame(p,&frame));
        assert(frame.trigger_sample>previous && atomic_load(&processed_samples)==depth);
        previous=frame.trigger_sample;
    }
    capture_pipeline_metrics(p,&metrics);
    assert(!metrics.source_error && !metrics.dropped && !metrics.rejected_blocks);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&available,0);atomic_store(&hardware_bounds,0);atomic_store(&burst_samples,0);
    atomic_store(&packed_peaks,0);atomic_store(&burst_period,0);atomic_store(&burst_width,32);
}

static void encoded_window_test(void)
{
    ZynqCaptureSource source;ZynqCapturePacket packet;
    double gain[2]={.004,.004},zero[2]={128,128};
    atomic_store(&hardware_bounds,1);atomic_store(&hardware_window_bounds,1);
    atomic_store(&burst_samples,2048);atomic_store(&packed_peaks,1);
    atomic_store(&burst_jitter,40);atomic_store(&burst_at,1000000);
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_configure(&source,1,10));
    for(unsigned i=0;i<4;++i){
        atomic_store(&available,1);assert(zynq_capture_dequeue(&source,&packet,1)==1);
        if(i<3)assert(!packet.window_bounds);
        else {
            assert(packet.bounds_valid && packet.window_bounds && packet.bounds_sequence==1);
            assert(packet.minimum[0]==60 && packet.maximum[0]==140);
            assert(packet.minimum_sum[0]==1660 && packet.maximum_sum[0]==1740 && packet.maximum_variance[0]==460800);
        }
        assert(zynq_capture_release_index(&source,packet.index));
    }
    for(int fault=1;fault<=5;++fault){
        atomic_store(&bounds_fault,fault);atomic_store(&available,1);
        assert(zynq_capture_dequeue(&source,&packet,1)==1 && !packet.bounds_valid);
        assert(zynq_capture_release_index(&source,packet.index));
    }
    zynq_capture_close(&source);atomic_store(&bounds_fault,0);
    atomic_store(&hardware_bounds,0);atomic_store(&hardware_window_bounds,0);
    atomic_store(&burst_samples,0);atomic_store(&packed_peaks,0);atomic_store(&burst_jitter,0);
}

static void encoded_dsp_test(void)
{
    ZynqCaptureSource source;ZynqCapturePacket packet;
    double gain[2]={.004,.004},zero[2]={128,128};
    struct osc_capture_dsp config={.control=OSC_CAPTURE_DSP_ENABLE,.level=140<<16,.hysteresis=2u<<16,.arm_id=7};
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_set_dsp(&source,&config) && source.dsp_supported<0);
    zynq_capture_close(&source);
    atomic_store(&supports_dsp,1);atomic_store(&dsp_fixture,1);dsp_fixture_sample=CAPTURE_NO_TRIGGER;
    assert(zynq_capture_open(&source,"/dev/test-adc",gain,zero));
    assert(zynq_capture_set_dsp(&source,&config) && source.dsp_supported==1);
    assert(zynq_capture_configure(&source,0,5));
    atomic_store(&available,1);assert(zynq_capture_dequeue(&source,&packet,1)==1);
    assert(packet.metadata.sequence==0 && packet.dsp_valid && !packet.dsp_triggered && packet.dsp_arm_id==7);
    ++config.arm_id;assert(zynq_capture_set_dsp(&source,&config));
    assert(packet.dsp_arm_id==7); /* An atomic re-arm does not mutate a leased header. */
    assert(zynq_capture_release_index(&source,packet.index));
    dsp_fixture_sample=32;
    atomic_store(&available,1);assert(zynq_capture_dequeue(&source,&packet,1)==1);
    assert(packet.dsp_valid && packet.dsp_triggered && packet.dsp_sample==32 && packet.dsp_arm_id==8 && !packet.bounds_valid);
    assert(zynq_capture_release_index(&source,packet.index));
    for(int fault=1;fault<=6;++fault){
        atomic_store(&bounds_fault,fault);atomic_store(&available,1);
        assert(zynq_capture_dequeue(&source,&packet,1)==1 && !packet.dsp_valid);
        assert(zynq_capture_release_index(&source,packet.index));
    }
    zynq_capture_close(&source);
    atomic_store(&bounds_fault,0);atomic_store(&supports_dsp,0);atomic_store(&dsp_fixture,0);
    memset(&dsp_configuration,0,sizeof(dsp_configuration));dsp_fixture_sample=CAPTURE_NO_TRIGGER;
}
static void hardware_dsp_pipeline_test(unsigned depth,double duration,unsigned channel,unsigned edge)
{
    CaptureRequest request={0};CaptureMetrics metrics;static DisplayFrame frame;
    double gain[2]={.004,.004},zero[2]={128,128};
    unsigned rate=osc_capture_rate(capture_rate_index(duration,depth,0));
    unsigned incoming=(rate==200000000?4u:2u)<<capture_payload_shift_for_depth(rate,depth);
    uint64_t event=70*(uint64_t)incoming+10+(edge?256:0);
    unsigned configured=atomic_load(&configurations);
    atomic_store(&supports_dsp,1);atomic_store(&dsp_fixture,2);
    atomic_store(&burst_samples,incoming);atomic_store(&packed_peaks,rate!=200000000);
    atomic_store(&burst_at,event-(edge?256:0));atomic_store(&burst_width,256);
    atomic_store(&burst_channel,channel);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    capture_pipeline_presented(p,0);
    request.running=1;request.duration_seconds=duration;request.record_samples=depth;
    request.pan_fraction=.5;request.trigger_mode=1;request.trigger_channel=(int)channel;
    request.trigger_edge=(int)edge;request.trigger_voltage=.048;request.filter_level[channel]=2;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100 && atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    atomic_store(&available,80);
    /* The fixture synthesizes up to 80 large payloads on the host. Allow
       instrumentation time without imposing its throughput on capture. */
    int found=0;
    for(unsigned i=0;i<1000;++i){
        if(capture_pipeline_frame(p,&frame)){found=1;break;}
        scope_sleep_ms(5);
    }
    assert(found);
    assert(frame.trigger_sample==event && atomic_load(&processed_samples)==depth);
    for(unsigned i=0;i<200 && atomic_load(&available);++i)scope_sleep_ms(5);
    capture_pipeline_metrics(p,&metrics);
    assert(metrics.dsp_blocks>=70 && metrics.rejected_blocks>=60 && !metrics.dsp_missed && !metrics.source_error && !metrics.overwritten);
    assert(dsp_configuration.level==140*65536 && dsp_configuration.hysteresis==2u*65536);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&available,0);atomic_store(&supports_dsp,0);atomic_store(&dsp_fixture,0);
    atomic_store(&burst_samples,0);atomic_store(&packed_peaks,0);atomic_store(&burst_channel,0);atomic_store(&burst_width,32);
    memset(&dsp_configuration,0,sizeof(dsp_configuration));dsp_fixture_sample=dsp_last_sample=CAPTURE_NO_TRIGGER;
}
static void complete_encoded_record_test(int peaks,int force)
{
    CaptureRequest request={0};CaptureMetrics metrics;static DisplayFrame frame;
    double gain[2]={.004,.004},zero[2]={128,128};
    const unsigned depth=1000000;double duration=peaks?.01:.001;
    unsigned rate=osc_capture_rate(capture_rate_index(duration,depth,0));
    unsigned incoming=(rate==200000000?4u:2u)<<capture_payload_shift_for_depth(rate,depth);
    unsigned configured=atomic_load(&configurations);
    atomic_store(&supports_dsp,1);atomic_store(&dsp_fixture,1);
    dsp_fixture_sample=CAPTURE_NO_TRIGGER;atomic_store(&burst_samples,incoming);
    atomic_store(&packed_peaks,peaks);atomic_store(&burst_at,UINT64_C(1)<<60);
    atomic_store(&burst_width,32);atomic_store(&summary_gap,peaks);
    atomic_store(&slow_bulk_decode,1);
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    request.running=!force;request.force_sequence=force?1:0;
    request.duration_seconds=duration;request.record_samples=depth;request.pan_fraction=.5;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100 && atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    atomic_store(&available,16);
    assert(wait_frame(p,&frame) && atomic_load(&processed_samples)==depth);
    assert(frame.view_samples==(size_t)(duration*rate));
    capture_pipeline_metrics(p,&metrics);assert(metrics.waveforms && !metrics.source_error);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&available,0);atomic_store(&slow_bulk_decode,0);atomic_store(&summary_gap,0);
    atomic_store(&supports_dsp,0);atomic_store(&dsp_fixture,0);atomic_store(&packed_peaks,0);
    atomic_store(&burst_samples,0);atomic_store(&burst_at,0);
    memset(&dsp_configuration,0,sizeof(dsp_configuration));dsp_fixture_sample=CAPTURE_NO_TRIGGER;
}

static void first_presented_trigger_test(unsigned depth,double duration,int hardware_dsp)
{
    CaptureRequest request={0};CaptureMetrics metrics;
    static DisplayFrame frame;
    double gain[2]={.004,.004},zero[2]={128,128};
    unsigned configured=atomic_load(&configurations);
    unsigned samples=2u<<capture_payload_shift_for_depth(50000000,depth);
    atomic_store(&hardware_bounds,1);atomic_store(&burst_samples,samples);
    atomic_store(&packed_peaks,1);atomic_store(&burst_at,0);
    atomic_store(&burst_width,UINT32_MAX);atomic_store(&burst_period,512);
    if(hardware_dsp){atomic_store(&supports_dsp,1);atomic_store(&dsp_fixture,2);}
    CapturePipeline *p=capture_pipeline_create_zynq("/dev/test-adc",gain,zero);assert(p);
    capture_pipeline_presented(p,0);
    request.running=1;request.duration_seconds=duration;request.record_samples=depth;
    request.pan_fraction=.5;request.trigger_mode=1;request.filter_level[0]=2;
    request.zero_y[0]=request.zero_y[1]=200;request.pixels_per_volt[0]=request.pixels_per_volt[1]=100;
    capture_pipeline_request(p,&request);
    for(unsigned i=0;i<100&&atomic_load(&configurations)==configured;++i)scope_sleep_ms(5);
    atomic_store(&available,8);
    int found=0;
    for(unsigned i=0;i<100;++i){
        if(capture_pipeline_frame(p,&frame)){found=1;break;}scope_sleep_ms(5);
    }
    assert(found);
    uint64_t first_id=frame.capture_id,first_trigger=frame.trigger_sample;
    uint64_t span=(uint64_t)llround(duration*50000000),expected=((span/2+511)/512)*512;
    assert(first_id && first_trigger>=expected-8 && first_trigger<=expected+8);
    atomic_store(&available,8);
    for(unsigned i=0;i<1000&&atomic_load(&available);++i)scope_sleep_ms(5);
    assert(!atomic_load(&available));scope_sleep_ms(30);
    assert(!capture_pipeline_frame(p,&frame));
    for(unsigned i=0;i<100;++i){
        capture_pipeline_metrics(p,&metrics);
        if(metrics.waveforms>=2 && metrics.deadtime_blocks>=8)break;
        scope_sleep_ms(5);
    }
    assert(metrics.waveforms==2 && metrics.deadtime_blocks>=8 && !metrics.overwritten && !metrics.source_error);
    capture_pipeline_presented(p,first_id);atomic_store(&available,8);
    assert(wait_frame(p,&frame));
    if(hardware_dsp){
        expected=((depth+samples-1)/samples)*samples;
        if(expected<2u*samples)expected=2u*samples;
    }else expected=((span+samples-1)/samples)*samples;
    assert(frame.capture_id>first_id && frame.trigger_sample>=expected-8 && frame.trigger_sample<=expected+8);
    assert(atomic_load(&processed_samples)>=span && atomic_load(&processed_samples)<=depth);
    capture_pipeline_presented(p,frame.capture_id);
    assert(wait_frame(p,&frame));
    assert(atomic_load(&processed_samples)==depth);
    capture_pipeline_destroy(p);assert(!atomic_load(&leased));
    atomic_store(&available,0);atomic_store(&hardware_bounds,0);atomic_store(&burst_samples,0);
    atomic_store(&packed_peaks,0);atomic_store(&burst_period,0);atomic_store(&burst_width,32);
    if(hardware_dsp){
        atomic_store(&supports_dsp,0);atomic_store(&dsp_fixture,0);
        memset(&dsp_configuration,0,sizeof(dsp_configuration));dsp_fixture_sample=dsp_last_sample=CAPTURE_NO_TRIGGER;
    }
}

int main(void)
{
    assert((4u<<capture_payload_shift_for_depth(200000000,100000))==131072);
    assert((2u<<capture_payload_shift_for_depth(100000000,100000))==131072);
    assert((2u<<capture_payload_shift_for_depth(50000000,100000))==131072);
    assert(capture_payload_shift_for_depth(200000000,1000000)==capture_payload_shift(200000000));
    assert(capture_payload_shift_for_depth(2000,100000)==capture_payload_shift(2000));
    assert(capture_probe_power_code(39.65)==39 && capture_probe_power_code(127.5)==95);
    assert(capture_probe_power_code(4031)==254 && capture_probe_power_code(4032)==256);
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
    assert(zynq_capture_release(&source));
    {
        uint8_t data[130],peaks[130];
        memset(data,222,sizeof(data));memset(peaks,223,sizeof(peaks));
        atomic_store(&available,1);
        assert(zynq_capture_acquire_into(&source,&a,1,data+1,peaks+1,128)==1);
        assert(a.data==data+1 && a.peaks==peaks+1 && a.capacity==128 && a.bytes==128);
        for(unsigned pos=0;pos<a.bytes;pos+=4){
            assert(a.data[pos]==128 && a.data[pos+3]==131);
            assert(a.peaks[pos]==10 && a.peaks[pos+3]==250);
        }
        assert(data[0]==222 && data[129]==222 && peaks[0]==223 && peaks[129]==223);
        assert(zynq_capture_release(&source));atomic_store(&available,1);
        assert(zynq_capture_acquire_into(&source,&a,1,data,peaks,127)==1);
        assert(a.data!=data && a.peaks!=peaks); /* Safe fallback for a larger packet. */
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
    for(unsigned pan_step=0;pan_step<2;++pan_step){
        request.pan_fraction=pan_step?1:0;capture_pipeline_request(pipeline,&request);
        assert(wait_frame(pipeline,&frame));
        assert(frame.view_samples==128 && frame.y[0][SCOPE_PLOT_WIDTH-1]!=INT16_MIN);
    }
    atomic_store(&available,80);
    for(unsigned i=0;i<100&&atomic_load(&available);++i)scope_sleep_ms(5);
    assert(!atomic_load(&available));
    scope_sleep_ms(50);
    while(capture_pipeline_frame(pipeline,&frame)){}
    atomic_store(&available,1);assert(wait_frame(pipeline,&frame));
    assert(frame.view_samples==10000&&frame.trigger_sample==CAPTURE_NO_TRIGGER);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    for(unsigned pan_step=0;pan_step<5;++pan_step){
        request.pan_fraction=pan_step/4.0;capture_pipeline_request(pipeline,&request);
        assert(wait_frame(pipeline,&frame));
        for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    }
    scope_sleep_ms(50);while(capture_pipeline_frame(pipeline,&frame)){}
    atomic_fetch_add(&counter,5);atomic_store(&available,1);
    scope_sleep_ms(80);assert(!atomic_load(&available));
    assert(!capture_pipeline_frame(pipeline,&frame));
    atomic_store(&available,80);assert(wait_frame(pipeline,&frame));
    assert(frame.view_samples==10000);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(frame.y[0][x]!=INT16_MIN);
    for(unsigned i=0;i<100&&atomic_load(&available);++i)scope_sleep_ms(5);
    scope_sleep_ms(50);while(capture_pipeline_frame(pipeline,&frame)){}
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
    trigger_filter_support_test();
    adc_level_test();
    zero_correction_test();
    analysis_failure_test();
    baseline_fresh_capture_test();
    processing_overlap_test();
    {
        const unsigned depths[]={10000,50000,100000,500000,1000000};
        for(unsigned i=0;i<5;++i){
            unsigned rate=osc_capture_rate(capture_rate_index(.001,depths[i],0));
            unsigned samples=(rate==200000000?4u:2u)<<capture_payload_shift_for_depth(rate,depths[i]);
            for(int mode=0;mode<3;++mode){
                sparse_burst_test(depths[i],samples/2,mode,.5);
                sparse_burst_test(depths[i],0,mode,.5);
                sparse_burst_test(depths[i],samples-16,mode,.5);
            }
        }
        sparse_burst_test(10000,10000,1,0);
        sparse_burst_test(10000,10000,1,1);
        sparse_burst_test(10000,10001,1,0);
        sparse_burst_test(10000,10001,1,1);
    }
    startup_capture_test();
    encoded_packet_test();
    encoded_window_test();
    encoded_dsp_test();
    for(unsigned depth_index=0;depth_index<5;++depth_index)for(unsigned channel=0;channel<2;++channel)
        for(unsigned edge=0;edge<2;++edge)hardware_dsp_pipeline_test(capture_record_samples(depth_index),.0002,channel,edge);
    hardware_dsp_pipeline_test(1000000,.01,0,0);
    first_presented_trigger_test(100000,.001,0);
    first_presented_trigger_test(1000000,.01,0);
    first_presented_trigger_test(100000,.001,1);
    first_presented_trigger_test(1000000,.01,1);
    for(int edge=0;edge<=2;++edge){
        continuous_bounds_test(100000,edge);
        continuous_bounds_test(500000,edge);
    }
    const unsigned bound_samples=2u<<capture_payload_shift_for_depth(50000000,100000);
    for(int edge=0;edge<=2;++edge)for(int mode=1;mode<=2;++mode){
        hardware_bounds_burst_test(0,edge,mode,edge==1,0);
        hardware_bounds_burst_test(bound_samples/2,edge,mode,edge==1,0);
        hardware_bounds_burst_test(bound_samples-16,edge,mode,edge==1,0);
    }
    hardware_bounds_burst_test(bound_samples/2,0,1,1,0);
    hardware_bounds_burst_test(bound_samples/2,1,1,0,0);
    hardware_bounds_burst_test(bound_samples/2,2,1,1,0);
    for(int edge=0;edge<=2;++edge){
        hardware_bounds_burst_test(0,edge,1,edge==1,1);
        hardware_bounds_burst_test(bound_samples/2,edge,1,edge==1,1);
        hardware_bounds_burst_test(bound_samples-16,edge,1,edge==1,1);
        hardware_bounds_burst_test(bound_samples/2,edge,1,edge==1,2);
    }
    for(int peaks=0;peaks<2;++peaks)for(int force=0;force<2;++force)complete_encoded_record_test(peaks,force);
    return 0;
}
