#include "signal_measure.h"
#include "signal_edges.h"
#include <math.h>
#if defined(SIGNAL_TRACE_REPLAY)
#include "record_trace.h"
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
static unsigned minimum16(uint8x16_t values)
{
    uint8x8_t v=vpmin_u8(vget_low_u8(values),vget_high_u8(values));
    v=vpmin_u8(v,v);v=vpmin_u8(v,v);v=vpmin_u8(v,v);
    return vget_lane_u8(v,0);
}
static unsigned maximum16(uint8x16_t values)
{
    uint8x8_t v=vpmax_u8(vget_low_u8(values),vget_high_u8(values));
    v=vpmax_u8(v,v);v=vpmax_u8(v,v);v=vpmax_u8(v,v);
    return vget_lane_u8(v,0);
}
#endif
static SignalNumber value_at(const SignalTrace *s,SignalIndex i)
{
#if defined(SIGNAL_TRACE_REPLAY)
    return record_trace_value(s,i);
#else
    return s->format==SIGNAL_TRACE_VALUES?s->values[i]:(s->codes[i*s->stride]-s->zero)*s->gain;
#endif
}
static unsigned code_at(const SignalTrace *s,SignalIndex i)
{
#if defined(SIGNAL_TRACE_REPLAY)
    return record_trace_code(s,i);
#else
    return s->codes[i*s->stride];
#endif
}
/* Prepare one contiguous interval before entering the arithmetic pipeline.
   Ordinary memory needs no preparation; FPGA replay fills a bounded DDR tile. */
static SignalIndex prepare(const SignalTrace *s,SignalIndex first)
{
#if defined(SIGNAL_TRACE_REPLAY)
    return record_trace_prepare(s,first);
#else
    (void)first;return s->samples;
#endif
}
static void accumulate(SignalNumber value,SignalNumber *sum,SignalNumber *correction)
{
    SignalNumber adjusted=value-*correction,next=*sum+adjusted;
    *correction=(next-*sum)-adjusted;
    *sum=next;
}
void signal_u8_bounds(const uint8_t *codes,SignalIndex samples,SignalIndex stride,unsigned *low,unsigned *high)
{
    SignalIndex i=0;unsigned lo=255,hi=0;
#if defined(__ARM_NEON)
    if(stride==1 || stride==2){
        uint8x16_t minimum=vdupq_n_u8(255),maximum=vdupq_n_u8(0);
        for(;stride==2?samples-i>16:samples-i>=16;i+=16){
            uint8x16_t v=stride==2?vld2q_u8(codes+i*2).val[0]:vld1q_u8(codes+i);
            minimum=vminq_u8(minimum,v);maximum=vmaxq_u8(maximum,v);
        }
        lo=minimum16(minimum);hi=maximum16(maximum);
    }
#endif
    for(;i<samples;++i){unsigned v=codes[i*stride];if(v<lo)lo=v;if(v>hi)hi=v;}
    *low=lo;*high=hi;
}
void signal_measure_amplitude(const SignalTrace *s,SignalAmplitude *r)
{
    r->clipped=0;
    if(!s->samples){r->minimum=r->maximum=r->mean=r->rms=NAN;return;}
    if(s->format==SIGNAL_TRACE_VALUES){
        prepare(s,0);
        SignalNumber sum=0,squares=0,sum_error=0,square_error=0,low=value_at(s,0),high=low;
        SignalIndex i=0;
        while(i<s->samples){SignalIndex end=prepare(s,i);
            for(;i<end;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE
#endif
                SignalNumber x=value_at(s,i);
                accumulate(x,&sum,&sum_error);accumulate(x*x,&squares,&square_error);
                if(x<low)low=x;
                if(x>high)high=x;
            }
        }
        r->minimum=low;r->maximum=high;r->mean=sum/s->samples;r->rms=signal_number_sqrt(squares/s->samples);
    }else{
        uint64_t sum=0,squares=0;unsigned low=255,high=0;SignalIndex i=0;
#if defined(__ARM_NEON)
        if(s->stride==1 || s->stride==2)while(s->stride==2?s->samples-i>16:s->samples-i>=16){
            SignalIndex end=i+((s->samples-i-(s->stride==2))&~(SignalIndex)15);
            uint8x16_t minimum=vdupq_n_u8(255),maximum=vdupq_n_u8(0);
            uint32x4_t sums=vdupq_n_u32(0),powers=vdupq_n_u32(0);
            uint32_t lanes[4];
            if(end-i>65536)end=i+65536;
            for(;i<end;i+=16){
                uint8x16_t v=s->stride==2?vld2q_u8(s->codes+i*2).val[0]:vld1q_u8(s->codes+i);
                minimum=vminq_u8(minimum,v);maximum=vmaxq_u8(maximum,v);
                sums=vpadalq_u16(sums,vpaddlq_u8(v));
                powers=vpadalq_u16(powers,vmull_u8(vget_low_u8(v),vget_low_u8(v)));
                powers=vpadalq_u16(powers,vmull_u8(vget_high_u8(v),vget_high_u8(v)));
            }
            vst1q_u32(lanes,sums);for(unsigned k=0;k<4;++k)sum+=lanes[k];
            vst1q_u32(lanes,powers);for(unsigned k=0;k<4;++k)squares+=lanes[k];
            if(minimum16(minimum)<low)low=minimum16(minimum);
            if(maximum16(maximum)>high)high=maximum16(maximum);
        }
#endif
        while(i<s->samples){SignalIndex end=prepare(s,i);
            for(;i<end;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE II=1
#endif
                unsigned v=code_at(s,i);sum+=v;squares+=v*v;
                if(v<low)low=v;
                if(v>high)high=v;
            }
        }
        /* Center the integer moments before converting to float. A calibrated
           zero near 128 must not cancel two large, rounded code powers. */
        unsigned origin=s->zero<0?0:s->zero>255?255:(unsigned)(s->zero+SIGNAL_NUMBER(.5));
        int64_t centered_sum=(int64_t)sum-(int64_t)origin*(int64_t)s->samples;
        int64_t centered_squares=(int64_t)squares-2*(int64_t)origin*(int64_t)sum+
            (int64_t)origin*origin*(int64_t)s->samples;
        SignalNumber mean=(SignalNumber)centered_sum/s->samples,zero=s->zero-origin;
        r->minimum=((s->gain>=0?low:high)-s->zero)*s->gain;
        r->maximum=((s->gain>=0?high:low)-s->zero)*s->gain;
        r->mean=(mean-zero)*s->gain;
        r->rms=signal_number_sqrt(signal_number_max(0,(SignalNumber)centered_squares/s->samples-2*zero*mean+zero*zero))*signal_number_abs(s->gain);
        r->clipped=low<=1 || high>=254;
    }
}
/* Bounded, in-place heapsort also synthesizes without recursion, qsort or
   function pointers. The original periods/widths retain their pairing. */
static void sort_periods(SignalNumber *values,SignalIndex count)
{
    for(SignalIndex step=count/2+count;step>0;--step){
        SignalIndex root,end;
        if(step>count){root=step-count-1;end=count;}
        else{
            end=step;root=0;
            SignalNumber swap=values[0];values[0]=values[end-1];values[end-1]=swap;
            --end;
        }
        while(root<end/2){
            SignalIndex child=2*root+1;
            if(child+1<end && values[child]<values[child+1])++child;
            if(values[root]>=values[child])break;
            SignalNumber swap=values[root];values[root]=values[child];values[child]=swap;root=child;
        }
    }
}
void signal_measure_timing(const SignalTrace *s,SignalNumber sample_rate,SignalNumber minimum,SignalNumber maximum,
    SignalNumber workspace[SIGNAL_TIMING_WORKSPACE],SignalTiming *result)
{
    unsigned histogram[256]={0},code;SignalIndex count=s->samples,total=0,n=0,i;
    SignalNumber range=maximum-minimum,low=minimum,high=maximum;
    SignalIndex last_rise=SIGNAL_INDEX_MAX,last_fall=SIGNAL_INDEX_MAX;
    SignalNumber rise_fraction=0,fall_fraction=0,all_span=0,all_width=0,all_squares=0;
    SignalNumber span_error=0,width_error=0,square_error=0;
    SignalIndex capacity=count/2<SIGNAL_TIMING_CAPACITY?count/2:SIGNAL_TIMING_CAPACITY;
    result->frequency=0;result->duty=NAN;
    if(count<3 || !signal_number_finite(range) || range<4*signal_number_abs(s->gain) || range<=0 || !workspace)return;
    SignalNumber histogram_scale=255/range;
    i=0;
    while(i<count){SignalIndex end=prepare(s,i);
        for(;i<end;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE
#endif
            SignalNumber bin=(value_at(s,i)-minimum)*histogram_scale;
            unsigned code=bin<0?0:bin>255?255:(unsigned)bin;++histogram[code];
        }
    }
    for(code=0;code<256;++code){
        total+=histogram[code];if(total>count/20){low=minimum+code*range/255;break;}
    }
    total=0;
    for(code=0;code<256;++code){
        total+=histogram[code];if(total>=count-count/20){high=minimum+code*range/255;break;}
    }
    if(high-low<range*SIGNAL_NUMBER(.2)){low=minimum;high=maximum;}
    SignalNumber level=(low+high)*SIGNAL_NUMBER(.5),band=signal_number_max(2*signal_number_abs(s->gain),(high-low)*SIGNAL_NUMBER(.15));
    SignalNumber *periods=workspace,*widths=workspace+capacity,*ordered=workspace+2*capacity;
    prepare(s,0);
    SignalEdgeDetector detector;signal_edge_init(&detector,level,band,value_at(s,0));
    i=1;
    while(i<count){SignalIndex end=prepare(s,i);
        for(;i<end;++i){
#if defined(__SYNTHESIS__)
#pragma HLS PIPELINE
#endif
            int edge=signal_edge_advance(&detector,value_at(s,i),i);
            if(edge){
                SignalIndex at=detector.crossing_sample;
                SignalNumber fraction=signal_edge_fraction(&detector);detector.crossing_sample=SIGNAL_INDEX_MAX;
                if(edge<0){last_fall=at;fall_fraction=fraction;}
                else{
                    if(last_rise!=SIGNAL_INDEX_MAX && last_fall>last_rise && last_fall<at){
                        /* Subtract integer positions before adding fractions:
                           float must never round an absolute 1M timestamp. */
                        SignalNumber period=(SignalNumber)(at-last_rise)+(fraction-rise_fraction);
                        SignalNumber width=(SignalNumber)(last_fall-last_rise)+(fall_fraction-rise_fraction);
                        accumulate(period,&all_span,&span_error);
                        accumulate(width,&all_width,&width_error);
                        accumulate(period*period,&all_squares,&square_error);
                        if(n<capacity){periods[n]=ordered[n]=period;widths[n]=width;}++n;
                    }
                    last_rise=at;rise_fraction=fraction;
                }
            }
        }
    }
    if(n>capacity){
        SignalNumber mean=all_span/n,variance=all_squares/n-mean*mean;
        if(variance<=mean*mean*SIGNAL_NUMBER(.04)){result->frequency=n*sample_rate/all_span;result->duty=100*all_width/all_span;}
    }else if(n){
        sort_periods(ordered,n);
        SignalNumber median=(ordered[(n-1)/2]+ordered[n/2])*SIGNAL_NUMBER(.5),span=0,positive=0;
        SignalNumber accepted_span_error=0,positive_error=0;SignalIndex accepted=0;
        for(i=0;i<n;++i)if(signal_number_abs(periods[i]-median)<=median*SIGNAL_NUMBER(.2)){
            accumulate(periods[i],&span,&accepted_span_error);
            accumulate(widths[i],&positive,&positive_error);++accepted;
        }
        if(accepted && accepted>=n-n/4){result->frequency=accepted*sample_rate/span;result->duty=100*positive/span;}
    }
}
