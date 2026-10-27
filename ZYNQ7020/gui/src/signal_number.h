#ifndef SIGNAL_NUMBER_H
#define SIGNAL_NUMBER_H
#include <math.h>
/* The numerical algorithm is shared. FPGA replay uses float to fit XC7Z020;
   the desktop reference retains the existing double precision. */
#if defined(SIGNAL_NUMBER_FLOAT)
typedef float SignalNumber;
#define signal_number_abs fabsf
#define signal_number_sqrt sqrtf
#define signal_number_max fmaxf
#else
typedef double SignalNumber;
#define signal_number_abs fabs
#define signal_number_sqrt sqrt
#define signal_number_max fmax
#endif
#define SIGNAL_NUMBER(value) ((SignalNumber)(value))
#endif
