#ifndef RECORD_TRACE_H
#define RECORD_TRACE_H
#include "signal_measure.h"
/* Burst-backed sequential access. Each invocation invalidates both tiles;
   persistent DDR addresses may contain a different record on the next job. */
void record_trace_begin(void);
size_t record_trace_prepare(const SignalTrace *trace,size_t first);
unsigned record_trace_code(const SignalTrace *trace,size_t sample);
SignalNumber record_trace_value(const SignalTrace *trace,size_t sample);
#endif
