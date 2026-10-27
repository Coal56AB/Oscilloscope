#include "signal_filter.h"
#include <math.h>
#include <string.h>

/* Overlap belongs to the input, not to the output. The same local Wiener
   estimate and centred average serve the desktop and the DDR replay kernel. */
void signal_filter_trace(const SignalTrace *source,SignalNumber noise_rms,unsigned level,SignalNumber *result)
{
    enum { TILE=256, PADDING=SIGNAL_FILTER_DENOISE_RADIUS+32, INPUT=TILE+2*PADDING };
    uint8_t codes[2*INPUT];
    SignalNumber values[INPUT],temporary[INPUT],output[TILE];
    unsigned padding=signal_filter_padding(level,noise_rms>0);
    for(SignalIndex first=0;first<source->samples;first+=TILE){
        SignalIndex end=source->samples-first>TILE?first+TILE:source->samples;
        SignalIndex begin=first>padding?first-padding:0;
        SignalIndex limit=source->samples-end>padding?end+padding:source->samples;
        SignalIndex count=limit-begin;
        if(source->format==SIGNAL_TRACE_BYTES){
            SignalIndex bytes=(count-1)*source->stride+1;
            memcpy(codes,source->codes+begin*source->stride,bytes);
            if(noise_rms>0 && source->gain!=0){
                signal_filter_denoise_u8(codes,count,source->stride,
                    noise_rms/signal_number_abs(source->gain),values);
                for(SignalIndex i=0;i<count;++i)values[i]=(values[i]-source->zero)*source->gain;
            }else for(SignalIndex i=0;i<count;++i)values[i]=(codes[i*source->stride]-source->zero)*source->gain;
        }else{
            memcpy(values,source->values+begin,count*sizeof(*values));
            if(noise_rms>0){
                signal_filter_denoise(values,count,noise_rms,temporary);
                for(SignalIndex i=0;i<count;++i)values[i]=temporary[i];
            }
        }
        if(level)signal_filter_apply(values,count,level,temporary);
        for(SignalIndex i=0;i<end-first;++i)output[i]=level?temporary[first-begin+i]:values[first-begin+i];
        memcpy(result+first,output,(end-first)*sizeof(*output));
    }
}
