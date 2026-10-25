#include "signal_filter.h"
#include <math.h>
#include <string.h>

void signal_filter_denoise(const double *source,size_t samples,double noise_rms,double *result)
{
    size_t first=0,end=0,count=0;
    double sum=0,squares=0,noise=noise_rms*noise_rms;
    if(!isfinite(noise) || noise<=0){memcpy(result,source,samples*sizeof(*result));return;}
    for(size_t i=0;i<samples;++i){
        size_t left=i>8?i-8:0,right=samples-i>8?i+9:samples;
        for(;end<right;++end)if(isfinite(source[end])){sum+=source[end];squares+=source[end]*source[end];++count;}
        for(;first<left;++first)if(isfinite(source[first])){sum-=source[first];squares-=source[first]*source[first];--count;}
        if(!isfinite(source[i]) || !count){result[i]=NAN;continue;}
        double mean=sum/count,variance=fmax(0,squares/count-mean*mean);
        /* A resolved transition must not acquire a ramp from the local mean. */
        double weight=variance>=9*noise?1:variance>noise?1-noise/variance:0;
        result[i]=mean+weight*(source[i]-mean);
    }
}

void signal_filter_apply(const double *source, size_t samples, unsigned level, double *result)
{
    static const unsigned radii[] = {0, 2, 8, 32};
    size_t i, first = 0, end = 0, count = 0;
    unsigned radius = radii[level < 4 ? level : 3];
    double sum = 0;
    if (!radius) { memcpy(result, source, samples * sizeof(*result)); return; }
    for (i = 0; i < samples; ++i) {
        size_t left = i > radius ? i - radius : 0;
        size_t right = samples - i > radius ? i + radius + 1 : samples;
        for (; end < right; ++end)
            if (isfinite(source[end])) { sum += source[end]; ++count; }
        for (; first < left; ++first)
            if (isfinite(source[first])) { sum -= source[first]; --count; }
        result[i] = isfinite(source[i]) && count ? sum / count : NAN;
    }
}
