#ifndef ANALYSIS_ZYNQ_H
#define ANALYSIS_ZYNQ_H
#include "signal_measure.h"
typedef struct {
    int fd,failed;
    float *values;
    size_t capacity;
    SignalAnalyzer analyzer;
} ZynqAnalysis;
/* Missing hardware keeps the legacy numerical path available. Other open
   failures are errors; an active hardware job never falls back to the CPU. */
int zynq_analysis_open(ZynqAnalysis *analysis,const char *device);
void zynq_analysis_close(ZynqAnalysis *analysis);
#endif
