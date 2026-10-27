#ifndef RECORD_TRACE_H
#define RECORD_TRACE_H
#include "signal_measure.h"
/* Burst-backed sequential access. Each invocation invalidates both tiles;
   persistent DDR addresses may contain a different record on the next job. */
void record_trace_begin(void);
SignalIndex record_trace_prepare(const SignalTrace *trace,SignalIndex first);
unsigned record_trace_code(const SignalTrace *trace,SignalIndex sample);
SignalNumber record_trace_value(const SignalTrace *trace,SignalIndex sample);
#endif
