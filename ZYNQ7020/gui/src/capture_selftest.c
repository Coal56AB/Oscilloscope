#include "capture_selftest.h"
#include "capture_zynq.h"
#include <stdio.h>

int capture_selftest(const char *device, CaptureSelftestProgress progress, void *context,
                     char *error, size_t error_size)
{
    ZynqCaptureSource source;
    const double gain[2]={1,1},zero[2]={0,0};
    int shift,ok=0;
    if(!zynq_capture_open(&source,device,gain,zero)) {
        snprintf(error,error_size,"ADC DEVICE / FPGA VERSION");return 0;
    }
    for(shift=6;shift>=0;--shift) {
        unsigned clock=100000000u>>shift,minimum[4]={255,255,255,255},maximum[4]={0};
        uint64_t previous=0;
        unsigned block;
        if(progress)progress(clock,(unsigned)(6-shift),context);
        if(!zynq_capture_test_clock(&source,(unsigned)shift,17)) {
            snprintf(error,error_size,"ADC CLOCK ERROR / %.5g MHz",clock/1e6);goto out;
        }
        for(block=0;block<4;++block) {
            CaptureBuffer data;
            size_t i,bytes;
            int received=zynq_capture_acquire(&source,&data,2000);
            if(received!=1) {
                snprintf(error,error_size,"ADC %s / %.5g MHz",received==0?"TIMEOUT":"DMA ERROR",clock/1e6);goto out;
            }
            if(data.sample_rate_hz!=(uint64_t)clock*2 || data.bytes%4 ||
                (block && data.sequence<=previous) ||
                data.first_sample!=data.sequence*(data.bytes/2)) {
                snprintf(error,error_size,"ADC DATA ERROR / %.5g MHz",clock/1e6);goto out;
            }
            previous=data.sequence;
            /* Inspect real contiguous A/B samples, with a bounded CPU cost at
               the nominal 400 MB/s stream. The driver checks every header. */
            bytes=data.bytes<4096?data.bytes:4096;
            for(i=0;i<bytes;++i) {
                unsigned branch=(unsigned)(i%4),value=data.data[i];
                if(value<minimum[branch])minimum[branch]=value;
                if(value>maximum[branch])maximum[branch]=value;
            }
            if(!zynq_capture_release(&source)) {
                snprintf(error,error_size,"ADC RELEASE ERROR / %.5g MHz",clock/1e6);goto out;
            }
        }
        for(block=0;block<4;++block) {
            if(minimum[block]==maximum[block] && (minimum[block]==0 || minimum[block]==255)) {
                snprintf(error,error_size,"ADC CH%u-%c AT %u / %.5g MHz",
                    block%2+1,block<2?'A':'B',minimum[block],clock/1e6);goto out;
            }
        }
        fprintf(stderr,"ADC self-test %.5g MHz: CH1 A=%u..%u B=%u..%u CH2 A=%u..%u B=%u..%u\n",
            clock/1e6,minimum[0],maximum[0],minimum[2],maximum[2],
            minimum[1],maximum[1],minimum[3],maximum[3]);
    }
    ok=1;
out:
    zynq_capture_close(&source);
    return ok;
}
