#ifndef SIGNAL_NUMBER_H
#define SIGNAL_NUMBER_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
/* A replay record has at most 1M samples. Hardware indices do not inherit
   the 64-bit size_t of the Windows synthesis host. Desktop APIs keep size_t. */
#if defined(SIGNAL_TRACE_REPLAY)
typedef uint32_t SignalIndex;
#define SIGNAL_INDEX_MAX UINT32_MAX
#else
typedef size_t SignalIndex;
#define SIGNAL_INDEX_MAX SIZE_MAX
#endif
/* The numerical algorithm is shared. FPGA replay uses float to fit XC7Z020;
   the desktop reference retains the existing double precision. */
#if defined(SIGNAL_NUMBER_FLOAT)
typedef float SignalNumber;
#define signal_number_abs fabsf
#define signal_number_sqrt sqrtf
#define signal_number_max fmaxf
#define signal_number_min fminf
#else
typedef double SignalNumber;
#define signal_number_abs fabs
#define signal_number_sqrt sqrt
#define signal_number_max fmax
#define signal_number_min fmin
#endif
#define SIGNAL_NUMBER(value) ((SignalNumber)(value))
#endif
