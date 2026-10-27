#ifndef SIGNAL_MEASURE_H
#define SIGNAL_MEASURE_H
#include "signal_number.h"
#include <stddef.h>
#include <stdint.h>

enum { SIGNAL_TRACE_BYTES=0,SIGNAL_TRACE_VALUES=1 };
typedef struct {
    const uint8_t *codes;
    const SignalNumber *values;
    size_t samples,stride;
    SignalNumber gain,zero;
    unsigned format;
} SignalTrace;
typedef struct {
    SignalNumber minimum,maximum,mean,rms;
    unsigned clipped;
} SignalAmplitude;
typedef struct { SignalNumber frequency,duty; } SignalTiming;
#define SIGNAL_TIMING_CAPACITY 4096u
#define SIGNAL_TIMING_WORKSPACE (3u*SIGNAL_TIMING_CAPACITY)

/* format selects values already in physical units or byte codes using
   (code-zero)*gain and stride. Format never depends on a memory address. */
void signal_measure_amplitude(const SignalTrace *trace,SignalAmplitude *result);
void signal_measure_timing(const SignalTrace *trace,SignalNumber sample_rate,SignalNumber minimum,SignalNumber maximum,
    SignalNumber workspace[SIGNAL_TIMING_WORKSPACE],SignalTiming *result);
void signal_u8_bounds(const uint8_t *codes,size_t samples,size_t stride,unsigned *low,unsigned *high);
#endif
