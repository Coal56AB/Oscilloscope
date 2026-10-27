#include "signal_filter.h"
#include <math.h>
#include <string.h>
static const unsigned radii[] = {0, 2, 8, 32};
/* Window populations are bounded by 65. Exact compile-time reciprocals
   remove the feedback dependency of a variable divider in the sample loop. */
#define INVERSE(n) (SIGNAL_NUMBER(1)/(n))
static const SignalNumber inverse_counts[]={0,
    INVERSE(1),INVERSE(2),INVERSE(3),INVERSE(4),INVERSE(5),INVERSE(6),INVERSE(7),INVERSE(8),
    INVERSE(9),INVERSE(10),INVERSE(11),INVERSE(12),INVERSE(13),INVERSE(14),INVERSE(15),INVERSE(16),
    INVERSE(17),INVERSE(18),INVERSE(19),INVERSE(20),INVERSE(21),INVERSE(22),INVERSE(23),INVERSE(24),
    INVERSE(25),INVERSE(26),INVERSE(27),INVERSE(28),INVERSE(29),INVERSE(30),INVERSE(31),INVERSE(32),
    INVERSE(33),INVERSE(34),INVERSE(35),INVERSE(36),INVERSE(37),INVERSE(38),INVERSE(39),INVERSE(40),
    INVERSE(41),INVERSE(42),INVERSE(43),INVERSE(44),INVERSE(45),INVERSE(46),INVERSE(47),INVERSE(48),
    INVERSE(49),INVERSE(50),INVERSE(51),INVERSE(52),INVERSE(53),INVERSE(54),INVERSE(55),INVERSE(56),
    INVERSE(57),INVERSE(58),INVERSE(59),INVERSE(60),INVERSE(61),INVERSE(62),INVERSE(63),INVERSE(64),INVERSE(65)};
#undef INVERSE

unsigned signal_filter_padding(unsigned level,int denoise)
{
    return radii[level<4?level:3]+(denoise?SIGNAL_FILTER_DENOISE_RADIUS:0);
}

static SignalNumber denoise_value(SignalNumber value,SignalNumber mean,SignalNumber variance,SignalNumber noise)
{
    if(variance>=SIGNAL_FILTER_DENOISE_EDGE_POWER*noise)return value;
    return variance>noise?mean+(1-noise/variance)*(value-mean):mean;
}

void signal_filter_denoise_bounds(SignalNumber raw_low,SignalNumber raw_high,SignalNumber mean_low,SignalNumber mean_high,
    SignalNumber maximum_variance,SignalNumber noise_rms,SignalNumber *low,SignalNumber *high)
{
    SignalNumber noise=noise_rms*noise_rms;
    *low=raw_low;*high=raw_high;
    if(!(noise>0) || !signal_number_finite(noise))return;
    *low=signal_number_min(raw_low,mean_low);*high=signal_number_max(raw_high,mean_high);
    if(!signal_number_finite(maximum_variance) || maximum_variance<0 || mean_low>mean_high ||
       maximum_variance>=SIGNAL_FILTER_DENOISE_EDGE_POWER*noise)return;
    SignalNumber retained=maximum_variance>noise?1-noise/maximum_variance:0;
    *low=signal_number_min(mean_low,mean_low+retained*(raw_low-mean_low));
    *high=signal_number_max(mean_high,mean_high+retained*(raw_high-mean_high));
}

static SignalNumber filter_origin(const SignalNumber *source,SignalIndex samples)
{
    for(SignalIndex i=0;i<samples;++i)if(signal_number_finite(source[i]))return source[i];
    return 0;
}

void signal_filter_denoise_u8(const uint8_t *source,SignalIndex samples,SignalIndex stride,SignalNumber noise_rms,SignalNumber *result)
{
    /* At most 18 byte values before the outgoing sample is removed. */
    uint16_t sum=0;
    unsigned squares=0;
    SignalNumber noise=noise_rms*noise_rms;
    if(!signal_number_finite(noise) || noise<=0){for(SignalIndex i=0;i<samples;++i)result[i]=source[i*stride];return;}
    for(SignalIndex j=0;j<samples && j<SIGNAL_FILTER_DENOISE_RADIUS;++j){unsigned value=source[j*stride];sum+=value;squares+=value*value;}
    for(SignalIndex i=0;i<samples;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE II=1
#endif
        SignalIndex left=i>SIGNAL_FILTER_DENOISE_RADIUS?i-SIGNAL_FILTER_DENOISE_RADIUS:0;
        SignalIndex right=samples-i>SIGNAL_FILTER_DENOISE_RADIUS?i+SIGNAL_FILTER_DENOISE_RADIUS+1:samples;
        if(samples-i>SIGNAL_FILTER_DENOISE_RADIUS){unsigned value=source[(right-1)*stride];sum+=value;squares+=value*value;}
        if(i>SIGNAL_FILTER_DENOISE_RADIUS){unsigned value=source[(left-1)*stride];sum-=value;squares-=value*value;}
        uint8_t count=(uint8_t)(right-left);
        SignalNumber inverse=inverse_counts[count];
        SignalNumber variance=(count*squares-sum*sum)*inverse*inverse;
        unsigned value=source[i*stride];
        SignalNumber mean=value+((int)sum-(int)(count*value))*inverse;
        result[i]=denoise_value(value,mean,variance,noise);
    }
}

void signal_filter_denoise(const SignalNumber *source,SignalIndex samples,SignalNumber noise_rms,SignalNumber *result)
{
    uint8_t count=0;
    SignalNumber sum=0,squares=0,noise=noise_rms*noise_rms;
    if(!signal_number_finite(noise) || noise<=0){for(SignalIndex i=0;i<samples;++i)result[i]=source[i];return;}
    SignalNumber origin=filter_origin(source,samples);
    for(SignalIndex j=0;j<samples && j<SIGNAL_FILTER_DENOISE_RADIUS;++j)
        if(signal_number_finite(source[j])){SignalNumber value=source[j]-origin;sum+=value;squares+=value*value;++count;}
    for(SignalIndex i=0;i<samples;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE II=1
#endif
        SignalIndex left=i>SIGNAL_FILTER_DENOISE_RADIUS?i-SIGNAL_FILTER_DENOISE_RADIUS:0;
        SignalIndex right=samples-i>SIGNAL_FILTER_DENOISE_RADIUS?i+SIGNAL_FILTER_DENOISE_RADIUS+1:samples;
        if(samples-i>SIGNAL_FILTER_DENOISE_RADIUS && signal_number_finite(source[right-1])){
            SignalNumber value=source[right-1]-origin;sum+=value;squares+=value*value;++count;
        }
        if(i>SIGNAL_FILTER_DENOISE_RADIUS && signal_number_finite(source[left-1])){
            SignalNumber value=source[left-1]-origin;sum-=value;squares-=value*value;--count;
        }
        if(!signal_number_finite(source[i]) || !count){result[i]=NAN;continue;}
        SignalNumber inverse=inverse_counts[count];
        SignalNumber mean=sum*inverse,variance=squares*inverse-mean*mean;
        if(!(variance>0))variance=0;
        /* A resolved transition must not acquire a ramp from the local mean. */
        mean=source[i]+(sum-count*(source[i]-origin))*inverse;
        result[i]=denoise_value(source[i],mean,variance,noise);
    }
}

void signal_filter_apply(const SignalNumber *source, SignalIndex samples, unsigned level, SignalNumber *result)
{
    SignalIndex i;uint8_t count=0;
    unsigned radius = radii[level < 4 ? level : 3];
    SignalNumber sum = 0;
    if (!radius) { for(SignalIndex j=0;j<samples;++j)result[j]=source[j];return; }
    SignalNumber origin=filter_origin(source,samples);
    for(SignalIndex j=0;j<samples && j<radius;++j)if(signal_number_finite(source[j])){sum+=source[j]-origin;++count;}
    for (i = 0; i < samples; ++i) {
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE II=1
#endif
        SignalIndex left = i > radius ? i - radius : 0;
        SignalIndex right = samples - i > radius ? i + radius + 1 : samples;
        if(samples-i>radius && signal_number_finite(source[right-1])){sum+=source[right-1]-origin;++count;}
        if(i>radius && signal_number_finite(source[left-1])){sum-=source[left-1]-origin;--count;}
        SignalNumber inverse=inverse_counts[count];
        result[i] = signal_number_finite(source[i]) && count ?
            source[i]+(sum-count*(source[i]-origin))*inverse : NAN;
    }
}
