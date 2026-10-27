#ifdef NDEBUG
#undef NDEBUG
#endif
#include "analysis_zynq.h"
#include "analysis_uapi.h"
#include "capture_processor.h"
#include "waveform_view.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void record_measure(const uint8_t *,const float *,float *,uint32_t,uint32_t,uint32_t,float,float,float,uint32_t,uint32_t,float);
static unsigned jobs,closed,open_error,bad_abi,job_error,finish_error,pending,overlaps,filter_jobs,filter_error,no_filter;
static struct osc_analysis_result completed;
static uint32_t bits(float value){uint32_t word;memcpy(&word,&value,4);return word;}
static float number(uint32_t word){float value;memcpy(&value,&word,4);return value;}
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;assert(!strcmp(path,"/dev/test-analysis"));
    if(open_error){errno=(int)open_error;return -1;}return 73;
}
int __wrap_close(int fd){assert(fd==73);pending=0;++closed;return 0;}
int __wrap_ioctl(int fd,unsigned long command,...)
{
    va_list args;void *argument;
    assert(fd==73);va_start(args,command);argument=va_arg(args,void *);va_end(args);
    if(command==OSC_ANALYSIS_GET_INFO){
        struct osc_analysis_info *info=argument;
        *info=(struct osc_analysis_info){bad_abi?99:OSC_ANALYSIS_ABI,OSC_ANALYSIS_MAX_SAMPLES,
            OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY|OSC_ANALYSIS_ASYNC|
                (no_filter?0:OSC_ANALYSIS_FILTER),0};return 0;
    }
    if(command==OSC_ANALYSIS_FILTER_RECORD){
        struct osc_analysis_filter *job=argument;assert(!pending);++filter_jobs;
        if(filter_error){errno=EIO;return -1;}
        float *result=calloc(job->input.samples+8,sizeof(float));assert(result);
        record_measure((const uint8_t *)(uintptr_t)job->input.codes_address,
            (const float *)(uintptr_t)job->input.values_address,result,job->input.samples,job->input.stride,
            job->input.format,number(job->input.gain_bits),number(job->input.zero_bits),0,1,job->level,number(job->noise_bits));
        assert(result[7]==1);memcpy((void *)(uintptr_t)job->output_address,result+8,job->input.samples*sizeof(float));
        free(result);return 0;
    }
    if(command==OSC_ANALYSIS_FINISH){
        assert(pending);pending=0;
        if(finish_error){errno=EIO;return -1;}
        *(struct osc_analysis_result *)argument=completed;return 0;
    }
    assert(command==OSC_ANALYSIS_BEGIN && !pending);++jobs;
    if(job_error){errno=EIO;return -1;}
    struct osc_analysis_measure *job=argument;float result[8];
    record_measure((const uint8_t *)(uintptr_t)job->codes_address,
        (const float *)(uintptr_t)job->values_address,result,job->samples,job->stride,job->format,
        number(job->gain_bits),number(job->zero_bits),number(job->sample_rate_bits),0,0,0);
    for(unsigned i=0;i<8;++i)completed.bits[i]=bits(result[i]);
    pending=1;
    return 0;
}
void __real_waveform_view_reduce(const WaveformSource *,double,double,unsigned,double,double,size_t,int16_t *,int16_t *,int16_t *);
void __wrap_waveform_view_reduce(const WaveformSource *source,double first,double samples,unsigned channel,
    double zero,double scale,size_t columns,int16_t *representative,int16_t *minimum,int16_t *maximum)
{
    if(pending)++overlaps;
    __real_waveform_view_reduce(source,first,samples,channel,zero,scale,columns,representative,minimum,maximum);
}
static void near(double actual,double expected,double tolerance)
{
    if(!((isnan(actual)&&isnan(expected)) || fabs(actual-expected)<=tolerance))
        fprintf(stderr,"actual %.12g expected %.12g tolerance %.9g\n",actual,expected,tolerance);
    assert((isnan(actual)&&isnan(expected)) || fabs(actual-expected)<=tolerance);
}
static void coordinates(const int16_t *actual,const int16_t *expected,size_t count,int filtered)
{
    for(size_t i=0;i<count;++i){
        /* Binary32 filter output can straddle an integer pixel boundary.
           The physical-value tolerance is checked separately above. */
        int tolerance=filtered?1:0;
        assert(abs((int)actual[i]-(int)expected[i])<=tolerance);
    }
}
static void compare(CaptureBuffer *b,CaptureView *view,ZynqAnalysis *analysis)
{
    DisplayFrame *software=malloc(sizeof(*software)),*hardware=malloc(sizeof(*hardware));
    uint8_t *original=malloc(b->bytes);assert(software&&hardware&&original);
    memcpy(original,b->data,b->bytes);
    view->analyzer=NULL;assert(capture_process(b,view,software));
    unsigned before=jobs;view->analyzer=&analysis->analyzer;
    assert(capture_process(b,view,hardware) && jobs>before && !analysis->failed);
    for(unsigned ch=0;ch<2;++ch){
        near(hardware->minimum[ch],software->minimum[ch],2e-6);
        near(hardware->maximum[ch],software->maximum[ch],2e-6);
        near(hardware->mean[ch],software->mean[ch],2e-6);
        near(hardware->rms[ch],software->rms[ch],2e-6);
        near(hardware->frequency_hz[ch],software->frequency_hz[ch],.1);
        near(hardware->duty[ch],software->duty[ch],.01);
        near(hardware->raw_mean[ch],software->raw_mean[ch],2e-6);
        near(hardware->raw_rms[ch],software->raw_rms[ch],2e-6);
    }
    assert(software->clipped_mask==hardware->clipped_mask);
    for(unsigned ch=0;ch<2;++ch){
        int filtered=view->filter_level[ch] || view->noise_rms[ch]>0;
        coordinates(hardware->y[ch],software->y[ch],SCOPE_PLOT_WIDTH,filtered);
        coordinates(hardware->y_min[ch],software->y_min[ch],SCOPE_PLOT_WIDTH,filtered);
        coordinates(hardware->y_max[ch],software->y_max[ch],SCOPE_PLOT_WIDTH,filtered);
        coordinates(hardware->peak_min[ch],software->peak_min[ch],SCOPE_PLOT_WIDTH,filtered);
        coordinates(hardware->peak_max[ch],software->peak_max[ch],SCOPE_PLOT_WIDTH,filtered);
    }
    assert(!memcmp(original,b->data,b->bytes));
    free(original);free(software);free(hardware);
}
int main(void)
{
    ZynqAnalysis analysis;CaptureView view={0};CaptureBuffer b={0};
    open_error=ENODEV;assert(zynq_analysis_open(&analysis,"/dev/test-analysis") && analysis.fd<0);
    zynq_analysis_close(&analysis);open_error=ENOENT;
    assert(zynq_analysis_open(&analysis,"/dev/test-analysis") && analysis.fd<0);
    open_error=EBUSY;assert(!zynq_analysis_open(&analysis,"/dev/test-analysis"));
    open_error=0;bad_abi=1;assert(!zynq_analysis_open(&analysis,"/dev/test-analysis") && closed==1);
    bad_abi=0;assert(zynq_analysis_open(&analysis,"/dev/test-analysis") && analysis.fd==73);
    b.data=malloc(2000000);b.capacity=2000000;b.first_sample=0;b.trigger_sample=CAPTURE_NO_TRIGGER;
    b.sample_rate_hz=1100000;b.volts_per_code[0]=.01;b.volts_per_code[1]=.02;
    b.zero_code[0]=128.1;b.zero_code[1]=126.25;assert(b.data);
    for(size_t i=0;i<1000000;++i){b.data[2*i]=i%100<33?195:85;b.data[2*i+1]=i%80<40?174:90;}
    view.skip_spectrum=1;view.zero_y[0]=100;view.zero_y[1]=200;
    view.pixels_per_volt[0]=view.pixels_per_volt[1]=50;
    static const size_t counts[]={1000,100000,1000000};
    for(unsigned k=0;k<3;++k){b.bytes=counts[k]*2;view.samples=counts[k];compare(&b,&view,&analysis);}
    b.bytes=200000;view.display_first=135.25;view.display_samples=9999.5;
    for(unsigned level=0;level<4;++level){
        view.filter_level[0]=level;view.filter_level[1]=3-level;
        view.noise_rms[0]=.02;view.noise_rms[1]=.04;
        compare(&b,&view,&analysis);
    }
    b.bytes=2000000;view.display_first=0;view.display_samples=1000000;
    compare(&b,&view,&analysis); /* Full-size processed array remains alive until FINISH. */
    b.bytes=200000;view.display_first=135.25;view.display_samples=9999.5;
    uint8_t *peaks=malloc(b.bytes);assert(peaks);b.peaks=peaks;
    for(size_t i=0;i<b.bytes/4;++i){
        peaks[4*i]=60;peaks[4*i+2]=210;peaks[4*i+1]=70;peaks[4*i+3]=185;
    }
    view.density_enabled=1;view.density_rows=64;
    for(unsigned level=0;level<4;++level){view.filter_level[0]=level;compare(&b,&view,&analysis);}
    b.peaks=NULL;free(peaks);view.density_enabled=0;
    double *corrected=malloc(b.bytes/2*sizeof(double));assert(corrected);
    for(size_t i=0;i<b.bytes/2;++i)corrected[i]=(i%101<33?.73125:-.385)+.0025*(i%7);
    view.corrected[0]=corrected;compare(&b,&view,&analysis);view.corrected[0]=NULL;free(corrected);
    view.hidden_channel_mask=2;compare(&b,&view,&analysis);
    view.display_first=-100000;compare(&b,&view,&analysis);
    memset(&view,0,sizeof(view));view.samples=1000000;view.skip_spectrum=1;
    b.bytes=2000000;memset(b.data,128,b.bytes);compare(&b,&view,&analysis);
    b.data[0]=0;b.data[3]=255;view.samples=1000;compare(&b,&view,&analysis);
    DisplayFrame *frame=malloc(sizeof(*frame));assert(frame);
    job_error=1;view.analyzer=&analysis.analyzer;
    assert(!capture_process(&b,&view,frame) && analysis.failed);
    unsigned before=jobs;assert(!capture_process(&b,&view,frame) && jobs==before);
    zynq_analysis_close(&analysis);assert(closed==2 && overlaps);
    job_error=0;finish_error=1;
    assert(zynq_analysis_open(&analysis,"/dev/test-analysis"));view.analyzer=&analysis.analyzer;
    assert(!capture_process(&b,&view,frame) && analysis.failed && !pending);
    zynq_analysis_close(&analysis);assert(closed==3 && filter_jobs);
    finish_error=0;filter_error=1;
    assert(zynq_analysis_open(&analysis,"/dev/test-analysis"));view.analyzer=&analysis.analyzer;
    view.noise_rms[0]=.02;assert(!capture_process(&b,&view,frame) && analysis.failed);
    before=filter_jobs;assert(!capture_process(&b,&view,frame) && filter_jobs==before);
    free(frame);free(b.data);zynq_analysis_close(&analysis);assert(closed==4);
    no_filter=1;assert(zynq_analysis_open(&analysis,"/dev/test-analysis") && !analysis.analyzer.filter);
    zynq_analysis_close(&analysis);assert(closed==5);
    return 0;
}
