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
        assert(((rate==200000000?4u:2u)<<shift)/(double)rate<=0.04);
        assert(shift<=17);
    }
    capture_history_init(&history,ring,NULL,10);
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
    {
        uint8_t peak_ring[20],peak_input[12],peak_output[20];
        capture_history_init(&history,ring,peak_ring,10);
        input.first_sample=0;input.peaks=peak_input;
        snapshot.peaks=peak_output;
        for(i=0;i<12;++i){samples[i]=(uint8_t)i;peak_input[i]=(uint8_t)(100+i);}
        capture_history_append(&history,&input);
        input.first_sample=6;
        for(i=0;i<12;++i){samples[i]=(uint8_t)(12+i);peak_input[i]=(uint8_t)(112+i);}
        capture_history_append(&history,&input);
        assert(capture_history_snapshot(&history,&snapshot) && snapshot.first_sample==2);
        for(i=0;i<20;++i){assert(storage[i]==i+4);assert(peak_output[i]==104+i);}
        /* A producer may write directly in the reserved tail. The same path
           must compact an overlapping tail correctly after a sequence gap. */
        for(unsigned gap=0;gap<2;++gap){
            uint8_t *target,*peak_target;
            input.first_sample=gap?100:12;
            assert(capture_history_prepare(&history,&input,1,&target,&peak_target));
            for(i=0;i<12;++i){target[i]=(uint8_t)(24+gap*12+i);peak_target[i]=(uint8_t)(124+gap*12+i);}
            input.data=target;input.peaks=peak_target;input.first_sample=gap?100:12;
            capture_history_append(&history,&input);
            assert(capture_history_snapshot(&history,&snapshot));
            if(!gap){assert(snapshot.first_sample==8 && snapshot.bytes==20);
                for(i=0;i<20;++i){assert(storage[i]==16+i);assert(peak_output[i]==116+i);}}
            else {assert(snapshot.first_sample==100 && snapshot.bytes==12);
                for(i=0;i<12;++i){assert(storage[i]==36+i);assert(peak_output[i]==136+i);}}
        }
        uint8_t *target,*peak_target;
        input.bytes=22;assert(!capture_history_prepare(&history,&input,1,&target,&peak_target));
    }
    return 0;
}
