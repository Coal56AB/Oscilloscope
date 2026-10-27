#include "record_trace.h"
#include <string.h>
enum { RAW_TILE=1024,VALUE_TILE=256 };
static uint8_t raw_tile[RAW_TILE];
static SignalNumber value_tile[VALUE_TILE];
static size_t raw_base,value_base;
void record_trace_begin(void){raw_base=SIZE_MAX;value_base=SIZE_MAX;}
size_t record_trace_prepare(const SignalTrace *trace,size_t sample)
{
    size_t end;
    if(trace->format==SIGNAL_TRACE_BYTES){
        size_t offset=trace->stride==2?sample*2:sample,base=offset&~(size_t)(RAW_TILE-1);
        if(base!=raw_base){
            size_t remaining=(trace->samples-1)*trace->stride+1-base;
            if(remaining>=RAW_TILE)memcpy(raw_tile,trace->codes+base,RAW_TILE);
            else for(size_t i=0;i<remaining;++i)raw_tile[i]=trace->codes[base+i];
            raw_base=base;
        }
        end=trace->stride==2?(base+RAW_TILE)/2:base+RAW_TILE;
    }else{
        size_t base=sample&~(size_t)(VALUE_TILE-1);
        if(base!=value_base){
            size_t remaining=trace->samples-base;
            if(remaining>=VALUE_TILE)memcpy(value_tile,trace->values+base,VALUE_TILE*sizeof(*value_tile));
            else for(size_t i=0;i<remaining;++i)value_tile[i]=trace->values[base+i];
            value_base=base;
        }
        end=base+VALUE_TILE;
    }
    return end<trace->samples?end:trace->samples;
}
unsigned record_trace_code(const SignalTrace *trace,size_t sample)
{
    size_t offset=trace->stride==2?sample*2:sample;
    return raw_tile[offset-raw_base];
}
SignalNumber record_trace_value(const SignalTrace *trace,size_t sample)
{
    if(trace->format==SIGNAL_TRACE_BYTES)return (record_trace_code(trace,sample)-trace->zero)*trace->gain;
    return value_tile[sample-value_base];
}
