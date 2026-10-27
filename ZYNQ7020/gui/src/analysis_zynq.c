#define _POSIX_C_SOURCE 200809L
#include "analysis_zynq.h"
#include "analysis_uapi.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static uint32_t bits(float value)
{
    uint32_t word;memcpy(&word,&value,sizeof(word));return word;
}
static double number(uint32_t word)
{
    float value;memcpy(&value,&word,sizeof(value));return value;
}
static int measure(void *context,const SignalTrace *trace,SignalNumber rate,
    SignalAmplitude *amplitude,SignalTiming *timing)
{
    ZynqAnalysis *a=context;
    struct osc_analysis_measure job={0};
    if(a->failed)return 0;
    if(trace->samples>OSC_ANALYSIS_MAX_SAMPLES || trace->stride>2){a->failed=1;return 0;}
    if(trace->format==SIGNAL_TRACE_VALUES){
        if(trace->samples>a->capacity){
            float *values=realloc(a->values,trace->samples*sizeof(*values));
            if(!values){a->failed=1;return 0;}
            a->values=values;a->capacity=trace->samples;
        }
        for(size_t i=0;i<trace->samples;++i)a->values[i]=(float)trace->values[i];
        job.values_address=(uintptr_t)a->values;job.format=1;
    }else job.codes_address=(uintptr_t)trace->codes;
    job.samples=(uint32_t)trace->samples;job.stride=(uint32_t)trace->stride;
    job.gain_bits=bits((float)trace->gain);job.zero_bits=bits((float)trace->zero);
    job.sample_rate_bits=bits((float)rate);
    if(ioctl(a->fd,OSC_ANALYSIS_MEASURE,&job)<0 || job.result_bits[7]!=bits(1)){
        a->failed=1;return 0;
    }
    amplitude->minimum=number(job.result_bits[0]);amplitude->maximum=number(job.result_bits[1]);
    amplitude->mean=number(job.result_bits[2]);amplitude->rms=number(job.result_bits[3]);
    amplitude->clipped=number(job.result_bits[6])!=0;
    timing->frequency=number(job.result_bits[4]);timing->duty=number(job.result_bits[5]);
    return 1;
}
int zynq_analysis_open(ZynqAnalysis *a,const char *device)
{
    struct osc_analysis_info info;
    memset(a,0,sizeof(*a));a->fd=-1;
    a->fd=open(device,O_RDWR|O_CLOEXEC);
    if(a->fd<0)return errno==ENODEV || errno==ENOENT;
    if(ioctl(a->fd,OSC_ANALYSIS_GET_INFO,&info)<0 || info.abi_version!=OSC_ANALYSIS_ABI ||
       info.max_samples!=OSC_ANALYSIS_MAX_SAMPLES || info.reserved ||
       (info.features&(OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY))!=
           (OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY)){
        zynq_analysis_close(a);return 0;
    }
    a->analyzer=(SignalAnalyzer){a,measure};
    return 1;
}
void zynq_analysis_close(ZynqAnalysis *a)
{
    if(a->fd>=0)close(a->fd);
    free(a->values);memset(a,0,sizeof(*a));a->fd=-1;
}
