#define _POSIX_C_SOURCE 200809L
#include "analysis_uapi.h"
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
_Static_assert(sizeof(struct osc_analysis_measure)==80,"analysis ABI layout");
static uint32_t bits(float value)
{
    uint32_t word;memcpy(&word,&value,sizeof(word));return word;
}
static float number(uint32_t word)
{
    float value;memcpy(&value,&word,sizeof(value));return value;
}
static double seconds(void)
{
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;
}
static void near(float actual,double expected,double tolerance)
{
    if(!(fabs(actual-expected)<=tolerance)){
        fprintf(stderr,"actual %.9g expected %.12g tolerance %.9g\n",actual,expected,tolerance);exit(1);
    }
}
static struct osc_analysis_measure job(const uint8_t *codes,const float *values,unsigned count,unsigned channel,unsigned format)
{
    struct osc_analysis_measure result={0};
    result.codes_address=(uintptr_t)(codes+channel);result.values_address=(uintptr_t)values;
    result.samples=count;result.stride=2;result.format=format;
    result.gain_bits=bits(.01f);result.zero_bits=bits(128);result.sample_rate_bits=bits(1100000);
    return result;
}
static void execute(int fd,struct osc_analysis_measure *request)
{
    double begin=seconds();
    if(ioctl(fd,OSC_ANALYSIS_MEASURE,request)){perror("measure");exit(1);}
    near(number(request->result_bits[7]),1,0);
    printf("samples=%u format=%u elapsed_ms=%.3f min=%.8g max=%.8g mean=%.8g rms=%.8g frequency=%.8g duty=%.8g\n",
        request->samples,request->format,(seconds()-begin)*1000,
        number(request->result_bits[0]),number(request->result_bits[1]),
        number(request->result_bits[2]),number(request->result_bits[3]),
        number(request->result_bits[4]),number(request->result_bits[5]));
}
int main(void)
{
    static const unsigned counts[]={1000,10000,100000,1000000};
    uint8_t *codes=malloc(2000000);float *values=malloc(1000000*sizeof(float));
    struct osc_analysis_info info;
    struct osc_analysis_measure request;
    int fd=open("/dev/oscill-analysis",O_RDWR);
    if(fd<0){perror("open analysis");return 1;}
    assert(codes && values);
    assert(ioctl(fd,OSC_ANALYSIS_GET_INFO,&info)==0 && info.abi_version==OSC_ANALYSIS_ABI &&
        info.max_samples==1000000 && info.features==(OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY));
    errno=0;assert(open("/dev/oscill-analysis",O_RDWR)<0 && errno==EBUSY);
    for(unsigned i=0;i<1000000;++i){
        codes[2*i]=i%100<33?195:85;codes[2*i+1]=i%100<67?175:95;
        values[i]=i%100<33?1.25f:-2.5f;
    }
    for(unsigned k=0;k<sizeof(counts)/sizeof(*counts);++k){
        request=job(codes,values,counts[k],0,0);execute(fd,&request);
        near(number(request.result_bits[0]),-.43,1e-5);near(number(request.result_bits[1]),.67,1e-5);
        near(number(request.result_bits[2]),-.067,1e-5);
        near(number(request.result_bits[3]),sqrt(.33*.67*.67+.67*.43*.43),1e-5);
        near(number(request.result_bits[4]),11000,.02);near(number(request.result_bits[5]),33,.02);
        request=job(codes,values,counts[k],0,0);request.sample_rate_bits=bits(0);execute(fd,&request);
        near(number(request.result_bits[2]),-.067,1e-5);near(number(request.result_bits[4]),0,0);
        assert(isnan(number(request.result_bits[5])));
        request=job(codes,values,counts[k],1,0);execute(fd,&request);
        near(number(request.result_bits[0]),-.33,1e-5);near(number(request.result_bits[1]),.47,1e-5);
        near(number(request.result_bits[2]),.206,1e-5);
        near(number(request.result_bits[4]),11000,.02);near(number(request.result_bits[5]),67,.02);
        request=job(codes,values,counts[k],0,1);request.stride=0;execute(fd,&request);
        near(number(request.result_bits[0]),-2.5,1e-5);near(number(request.result_bits[1]),1.25,1e-5);
        near(number(request.result_bits[2]),-1.2625,1e-5);
        near(number(request.result_bits[3]),sqrt(.33*1.25*1.25+.67*2.5*2.5),1e-5);
        near(number(request.result_bits[4]),11000,.02);near(number(request.result_bits[5]),33,.02);
    }
    memset(codes,128,2000000);
    request=job(codes,values,1000000,0,0);request.zero_bits=bits(128.1f);execute(fd,&request);
    near(number(request.result_bits[2]),-.001,1e-6);near(number(request.result_bits[3]),.001,1e-6);
    near(number(request.result_bits[4]),0,0);assert(isnan(number(request.result_bits[5])));
    request=job(codes,values,0,0,0);execute(fd,&request);
    assert(isnan(number(request.result_bits[0])) && isnan(number(request.result_bits[3])));
    request=job(codes,values,1000001,0,0);
    assert(ioctl(fd,OSC_ANALYSIS_MEASURE,&request)<0 && errno==EINVAL);
    request=job(codes,values,1000,0,0);request.reserved[0]=1;
    assert(ioctl(fd,OSC_ANALYSIS_MEASURE,&request)<0 && errno==EINVAL);
    request=job(codes,values,1000,0,0);request.sample_rate_bits=0x7fc00000;
    assert(ioctl(fd,OSC_ANALYSIS_MEASURE,&request)<0 && errno==EINVAL);
    close(fd);fd=open("/dev/oscill-analysis",O_RDWR);assert(fd>=0);
    request=job(codes,values,1000,0,0);execute(fd,&request);
    near(number(request.result_bits[2]),0,0);near(number(request.result_bits[3]),0,0);
    close(fd);free(codes);free(values);
    puts("PASS: FPGA replay, two channels, raw/processed, 1K..1M, small calibrated DC, ABI, bounds and reopen");
    return 0;
}
