#include "signal_filter.h"
#include <math.h>
#include <string.h>
static const unsigned radii[] = {0, 2, 8, 32};

unsigned signal_filter_padding(unsigned level,int denoise)
{
    return radii[level<4?level:3]+(denoise?SIGNAL_FILTER_DENOISE_RADIUS:0);
}

static double denoise_value(double value,double mean,double variance,double noise)
{
    if(variance>=SIGNAL_FILTER_DENOISE_EDGE_POWER*noise)return value;
    return variance>noise?mean+(1-noise/variance)*(value-mean):mean;
}

void signal_filter_denoise_bounds(double raw_low,double raw_high,double mean_low,double mean_high,
    double maximum_variance,double noise_rms,double *low,double *high)
{
    double noise=noise_rms*noise_rms;
    *low=raw_low;*high=raw_high;
    if(!(noise>0) || !isfinite(noise))return;
    *low=fmin(raw_low,mean_low);*high=fmax(raw_high,mean_high);
    if(!isfinite(maximum_variance) || maximum_variance<0 || mean_low>mean_high ||
       maximum_variance>=SIGNAL_FILTER_DENOISE_EDGE_POWER*noise)return;
    double retained=maximum_variance>noise?1-noise/maximum_variance:0;
    *low=fmin(mean_low,mean_low+retained*(raw_low-mean_low));
    *high=fmax(mean_high,mean_high+retained*(raw_high-mean_high));
}

static double filter_origin(const double *source,size_t samples)
{
    for(size_t i=0;i<samples;++i)if(isfinite(source[i]))return source[i];
    return 0;
}

void signal_filter_denoise_u8(const uint8_t *source,size_t samples,size_t stride,double noise_rms,double *result)
{
    size_t first=0,end=0,previous_count=0;
    unsigned sum=0,squares=0;
    double noise=noise_rms*noise_rms,inverse=0;
    if(!isfinite(noise) || noise<=0){for(size_t i=0;i<samples;++i)result[i]=source[i*stride];return;}
    for(size_t i=0;i<samples;++i){
        size_t left=i>SIGNAL_FILTER_DENOISE_RADIUS?i-SIGNAL_FILTER_DENOISE_RADIUS:0;
        size_t right=samples-i>SIGNAL_FILTER_DENOISE_RADIUS?i+SIGNAL_FILTER_DENOISE_RADIUS+1:samples;
        for(;end<right;++end){unsigned value=source[end*stride];sum+=value;squares+=value*value;}
        for(;first<left;++first){unsigned value=source[first*stride];sum-=value;squares-=value*value;}
        size_t count=end-first;
        if(count!=previous_count){inverse=1.0/count;previous_count=count;}
        double variance=(count*squares-sum*sum)*inverse*inverse;
        unsigned value=source[i*stride];
        double mean=value+((int)sum-(int)(count*value))*inverse;
        result[i]=denoise_value(value,mean,variance,noise);
    }
}

void signal_filter_denoise(const double *source,size_t samples,double noise_rms,double *result)
{
    size_t first=0,end=0,count=0;
    double sum=0,squares=0,noise=noise_rms*noise_rms,inverse=0;
    size_t previous_count=0;
    if(!isfinite(noise) || noise<=0){memcpy(result,source,samples*sizeof(*result));return;}
    double origin=filter_origin(source,samples);
    for(size_t i=0;i<samples;++i){
        size_t left=i>SIGNAL_FILTER_DENOISE_RADIUS?i-SIGNAL_FILTER_DENOISE_RADIUS:0;
        size_t right=samples-i>SIGNAL_FILTER_DENOISE_RADIUS?i+SIGNAL_FILTER_DENOISE_RADIUS+1:samples;
        for(;end<right;++end)if(isfinite(source[end])){double value=source[end]-origin;sum+=value;squares+=value*value;++count;}
        for(;first<left;++first)if(isfinite(source[first])){double value=source[first]-origin;sum-=value;squares-=value*value;--count;}
        if(!isfinite(source[i]) || !count){result[i]=NAN;continue;}
        if(count!=previous_count){inverse=1.0/count;previous_count=count;}
        double mean=sum*inverse,variance=squares*inverse-mean*mean;
        if(!(variance>0))variance=0;
        /* A resolved transition must not acquire a ramp from the local mean. */
        mean=source[i]+(sum-count*(source[i]-origin))*inverse;
        result[i]=denoise_value(source[i],mean,variance,noise);
    }
}

void signal_filter_apply(const double *source, size_t samples, unsigned level, double *result)
{
    size_t i, first = 0, end = 0, count = 0;
    unsigned radius = radii[level < 4 ? level : 3];
    double sum = 0,inverse=0;
    size_t previous_count=0;
    if (!radius) { memcpy(result, source, samples * sizeof(*result)); return; }
    double origin=filter_origin(source,samples);
    for (i = 0; i < samples; ++i) {
        size_t left = i > radius ? i - radius : 0;
        size_t right = samples - i > radius ? i + radius + 1 : samples;
        for (; end < right; ++end)
            if (isfinite(source[end])) { sum += source[end]-origin; ++count; }
        for (; first < left; ++first)
            if (isfinite(source[first])) { sum -= source[first]-origin; --count; }
        if(count && count!=previous_count){inverse=1.0/count;previous_count=count;}
        result[i] = isfinite(source[i]) && count ?
            source[i]+(sum-count*(source[i]-origin))*inverse : NAN;
    }
}
