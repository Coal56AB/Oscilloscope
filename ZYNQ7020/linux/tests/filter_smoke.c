#define _POSIX_C_SOURCE 200809L
#include "analysis_uapi.h"
#include "signal_filter.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
_Static_assert(sizeof(struct osc_analysis_filter)==96,"filter ABI layout");
static uint32_t bits(float value){uint32_t word;memcpy(&word,&value,4);return word;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void compare(int fd,const uint8_t *codes,const float *values,float *output,
    unsigned count,unsigned format,unsigned level,float noise)
{
    double *expected=malloc(2*count*sizeof(double));assert(expected);
    double *temporary=expected+count;
    if(format){
        for(unsigned i=0;i<count;++i)expected[i]=values[i];
        if(noise>0){signal_filter_denoise(expected,count,noise,temporary);memcpy(expected,temporary,count*sizeof(double));}
    }else{
        if(noise>0)signal_filter_denoise_u8(codes,count,2,noise/(double).01f,expected);
        else for(unsigned i=0;i<count;++i)expected[i]=codes[2*i];
        for(unsigned i=0;i<count;++i)expected[i]=(expected[i]-128.25f)*(double).01f;
    }
    if(level){signal_filter_apply(expected,count,level,temporary);memcpy(expected,temporary,count*sizeof(double));}
    struct osc_analysis_filter job={0};
    job.input.codes_address=(uintptr_t)codes;job.input.values_address=(uintptr_t)values;
    job.input.samples=count;job.input.stride=2;job.input.format=format;
    job.input.gain_bits=bits(.01f);job.input.zero_bits=bits(128.25f);
    job.output_address=(uintptr_t)output;job.level=level;job.noise_bits=bits(noise);
    double begin=seconds();assert(ioctl(fd,OSC_ANALYSIS_FILTER_RECORD,&job)==0);
    double elapsed=seconds()-begin,maximum=0;
    for(unsigned i=0;i<count;++i){
        if(isnan(expected[i])){assert(isnan(output[i]));continue;}
        double error=fabs(output[i]-expected[i]);if(error>maximum)maximum=error;
        if(!(error<.00001)){fprintf(stderr,"sample=%u actual=%.12g expected=%.12g\n",i,output[i],expected[i]);abort();}
    }
    printf("filter samples=%u format=%u level=%u noise=%.5g elapsed_ms=%.3f maximum_error=%.8g\n",
        count,format,level,noise,elapsed*1000,maximum);fflush(stdout);
    free(expected);
}
int main(void)
{
    const unsigned maximum=1000000;
    uint8_t *codes=malloc(2*maximum);float *values=malloc(maximum*sizeof(float)),*output=malloc(maximum*sizeof(float));
    assert(codes && values && output);
    int fd=open("/dev/oscill-analysis",O_RDWR);assert(fd>=0);
    struct osc_analysis_info info;assert(!ioctl(fd,OSC_ANALYSIS_GET_INFO,&info) && (info.features&OSC_ANALYSIS_FILTER));
    for(unsigned i=0;i<maximum;++i){
        codes[2*i]=(uint8_t)((i%101<33?190:80)+(i%7)-3);
        codes[2*i+1]=(uint8_t)((i%73<51?175:95)+(i%5)-2);
        values[i]=(codes[2*i]-128.25f)*.01f;
    }
    values[255]=NAN;values[256]=NAN;values[4096]=NAN;
    const unsigned lengths[]={1,257,100000};
    for(unsigned k=0;k<3;++k)for(unsigned level=0;level<4;++level){
        compare(fd,codes,values,output,lengths[k],0,level,.025f);
        compare(fd,codes+1,values,output,lengths[k],0,level,0);
        compare(fd,codes,values,output,lengths[k],1,level,.025f);
    }
    for(unsigned level=0;level<4;level+=3){
        compare(fd,codes,values,output,maximum,0,level,.025f);
        compare(fd,codes+1,values,output,maximum,0,level,.025f);
        compare(fd,codes,values,output,maximum,1,level,.025f);
    }
    memset(codes,128,2*maximum);for(unsigned i=0;i<maximum;++i)values[i]=.00125f;
    compare(fd,codes,values,output,maximum,0,3,.025f);
    compare(fd,codes,values,output,maximum,1,3,.025f);
    struct osc_analysis_filter invalid={0};invalid.input.samples=1000001;
    invalid.input.stride=2;invalid.input.gain_bits=bits(.01f);
    assert(ioctl(fd,OSC_ANALYSIS_FILTER_RECORD,&invalid)<0 && errno==EINVAL);
    invalid.input.samples=1;invalid.level=4;assert(ioctl(fd,OSC_ANALYSIS_FILTER_RECORD,&invalid)<0 && errno==EINVAL);
    invalid.level=0;invalid.noise_bits=bits(-1);assert(ioctl(fd,OSC_ANALYSIS_FILTER_RECORD,&invalid)<0 && errno==EINVAL);
    close(fd);free(codes);free(values);free(output);
    puts("PASS: FPGA Wiener/FILTER, two channels, raw/physical, 1..1M, tile borders, NaN, DC and bounds");
    return 0;
}
