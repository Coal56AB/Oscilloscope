#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_history.h"
#include "capture_settings.h"
#include "capture_debug.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    uint8_t ring[20], storage[20], samples[12];
    CaptureHistory history;
    CaptureBuffer input = {0}, snapshot = {0};
    unsigned i, depth, scale;
    const double spans[] = {1e-7,1e-6,0.001,0.1,1,2,5,10,20,50};
    const unsigned expected_rates[] = {200000000,100000000,50000000,20000000,10000000,5000000,
        2000000,1000000,500000,200000,100000,50000,20000,10000,5000,2000,1000,500,200,100};
    for (i=0;i<20;++i) assert(osc_capture_rate(i)==expected_rates[i]);
    assert(osc_capture_rate(capture_rate_index(5000000 * 10e-6,100000,1))==2000);
    assert(osc_capture_rate(capture_rate_index(5000000 * 10e-6,1000000,1))==20000);
    for (depth=0;depth<CAPTURE_DEPTH_COUNT;++depth) for(scale=0;scale<10;++scale) {
        unsigned count=capture_record_samples(depth), roll=spans[scale]>1;
        unsigned rate=osc_capture_rate(capture_rate_index(spans[scale],count,roll));
        unsigned shift=capture_payload_shift(rate);
        assert(rate*spans[scale]*(roll?1:2)<=count);
        assert((4u<<shift)/(double)rate<=0.04);
        assert(shift<=17);
    }
    capture_history_init(&history,ring,10);
    input.data=samples; input.capacity=input.bytes=12; input.sample_rate_hz=100;
    input.trigger_sample=CAPTURE_NO_TRIGGER;
    input.volts_per_code[0]=input.volts_per_code[1]=1;
    snapshot.data=storage;snapshot.capacity=sizeof(storage);
    for(i=0;i<12;++i)samples[i]=(uint8_t)i;
    capture_history_append(&history,&input);
    assert(capture_history_snapshot(&history,&snapshot)&&snapshot.bytes==12);
    assert(!memcmp(storage,samples,12));
    input.first_sample=6;
    for(i=0;i<12;++i)samples[i]=(uint8_t)(i+12);
    capture_history_append(&history,&input);
    assert(capture_history_snapshot(&history,&snapshot)&&snapshot.bytes==20&&snapshot.first_sample==2);
    for(i=0;i<20;++i)assert(storage[i]==i+4);
    input.first_sample=100; /* Lost blocks must leave a gap, not compress time. */
    capture_history_append(&history,&input);
    assert(capture_history_snapshot(&history,&snapshot)&&snapshot.bytes==12&&snapshot.first_sample==100);
    input.first_sample=106;input.sample_rate_hz=200;
    capture_history_append(&history,&input);
    assert(capture_history_snapshot(&history,&snapshot)&&snapshot.bytes==12&&snapshot.sample_rate_hz==200);
    {
        CaptureDebug debug={1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},10000};
        assert(capture_debug_replace(&input,&snapshot,&debug)); /* Decimated DEBUG may alias. */
        for(i=0;i<12;i+=2) {
            assert(storage[i]>=127&&storage[i]<=129);
            assert(storage[i+1]>=205&&storage[i+1]<=207);
        }
    }
    return 0;
}
