#ifndef SPECTRUM_BINS_H
#define SPECTRUM_BINS_H

#include <math.h>

/* Common amplitude scale for the spectrum and its level cursor. */
static inline double spectrum_db_at_level(double level)
{
    return -80.0 + level * 80.0 / 255.0;
}

static inline double spectrum_level_at_db(double db)
{
    return (db + 80.0) * 255.0 / 80.0;
}

/* Logarithmic axis with a linear origin: zero through Nyquist.
   The transition scale equals one FFT frequency step. */
static inline double spectrum_frequency_at(double fraction, double span_hz, unsigned size)
{
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return span_hz * 2.0 / size * expm1(log1p(size / 2.0) * fraction);
}

/* Maximum over each logarithmic display group retains narrow spectral peaks.
   Below the FFT resolution, repeated nearest frequencies fill empty groups. */
static inline void spectrum_bin_range(unsigned index, unsigned count, unsigned size,
                                      unsigned *first, unsigned *end)
{
    double half = size / 2.0;
    *first = index ? (unsigned)ceil(expm1(log1p(half) * (index - 0.5) / (count - 1))) : 0;
    *end = index + 1 < count ?
        (unsigned)ceil(expm1(log1p(half) * (index + 0.5) / (count - 1))) : size / 2 + 1;
    if (*end <= *first) {
        *first = (unsigned)(expm1(log1p(half) * index / (count - 1)) + 0.5);
        *end = *first + 1;
    }
}

static inline double spectrum_magnitude_at(const double *re,const double *im,unsigned size,double bin)
{
    unsigned first=(unsigned)floor(bin);
    double a=hypot(re[first],im[first]);
    if(first>=size/2)return a;
    return a+(bin-first)*(hypot(re[first+1],im[first+1])-a);
}

/* Project the piecewise-linear native magnitudes. Interpolated cell edges
   remove nearest-bin stairs; every native peak in a cell is retained exactly. */
static inline double spectrum_project(const double *re,const double *im,unsigned size,unsigned index,unsigned count)
{
    double scale=log1p(size/2.0)/(count-1);
    double left=index?expm1(scale*(index-.5)):0;
    double right=index+1<count?expm1(scale*(index+.5)):size/2.0;
    double result=fmax(spectrum_magnitude_at(re,im,size,left),spectrum_magnitude_at(re,im,size,right));
    unsigned first=(unsigned)ceil(left),last=(unsigned)floor(right);
    for(unsigned k=first;k<=last;++k){double value=hypot(re[k],im[k]);if(value>result)result=value;}
    return result;
}

#endif
