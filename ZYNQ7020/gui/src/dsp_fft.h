#ifndef DSP_FFT_H
#define DSP_FFT_H
#include <stddef.h>
/* In-place radix-two transform. Inverse includes division by count. */
void dsp_fft(double *real,double *imag,size_t count,int inverse);
#endif
