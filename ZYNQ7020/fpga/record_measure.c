#include "signal_measure.h"
#include "record_trace.h"
#include <math.h>
#include <stdint.h>

/* DDR replay of one selected channel interval. This numerical kernel has no
   knowledge of the UI or capture protocol; the caller supplies its units. */
void record_measure(const uint8_t *codes,const SignalNumber *values,SignalNumber *result,
    uint32_t count,uint32_t stride,uint32_t filtered,SignalNumber gain,SignalNumber zero,SignalNumber sample_rate)
{
#pragma HLS INTERFACE m_axi port=codes offset=slave bundle=raw_memory depth=2000000 max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=values offset=slave bundle=filtered_memory depth=1000000 max_read_burst_length=64 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=result offset=slave bundle=result_memory depth=8 max_write_burst_length=8 num_write_outstanding=2
#pragma HLS INTERFACE s_axilite port=codes bundle=control
#pragma HLS INTERFACE s_axilite port=values bundle=control
#pragma HLS INTERFACE s_axilite port=result bundle=control
#pragma HLS INTERFACE s_axilite port=count bundle=control
#pragma HLS INTERFACE s_axilite port=stride bundle=control
#pragma HLS INTERFACE s_axilite port=filtered bundle=control
#pragma HLS INTERFACE s_axilite port=gain bundle=control
#pragma HLS INTERFACE s_axilite port=zero bundle=control
#pragma HLS INTERFACE s_axilite port=sample_rate bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    static SignalNumber workspace[SIGNAL_TIMING_WORKSPACE];
    result[7]=0;
    if(count>1000000 || stride>2 || (!filtered && !stride) || filtered>1 ||
        !isfinite(gain) || !isfinite(zero) || !isfinite(sample_rate) || sample_rate<0)return;
    record_trace_begin();
    SignalTrace trace={codes,values,count,stride,gain,zero,filtered?SIGNAL_TRACE_VALUES:SIGNAL_TRACE_BYTES};
    SignalAmplitude amplitude;SignalTiming timing={0,NAN};
    signal_measure_amplitude(&trace,&amplitude);
    if(sample_rate>0)signal_measure_timing(&trace,sample_rate,amplitude.minimum,amplitude.maximum,workspace,&timing);
    result[0]=amplitude.minimum;result[1]=amplitude.maximum;
    result[2]=amplitude.mean;result[3]=amplitude.rms;
    result[4]=timing.frequency;result[5]=timing.duty;
    result[6]=amplitude.clipped;result[7]=1;
}
