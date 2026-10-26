#ifndef CAPTURE_SETTINGS_H
#define CAPTURE_SETTINGS_H
#include "capture_rates.h"
#define CAPTURE_DEPTH_COUNT 5
#define CAPTURE_MAX_SAMPLES 1000000u
static inline unsigned capture_record_samples(unsigned index)
{
    static const unsigned samples[] = {10000,50000,100000,500000,1000000};
    return index < CAPTURE_DEPTH_COUNT ? samples[index] : 100000;
}
static inline unsigned capture_rate_index(double seconds, unsigned samples, int roll)
{
    unsigned index = 0;
    double limit = samples / (seconds * (roll ? 1.0 : 2.0));
    /* Decimal time/div values can round just above an exact rate boundary. */
    while (index + 1 < OSC_CAPTURE_RATE_COUNT && osc_capture_rate(index) > limit * (1.0 + 1e-12)) ++index;
    return index;
}
static inline unsigned capture_payload_shift(unsigned rate)
{
    unsigned shift = 0;
    /* Peak groups hold two pairs/beat; full rate holds four. <=40 ms/block. */
    while (shift < 17 && ((rate==200000000?4u:2u) << (shift + 1)) <= rate / 25) ++shift;
    return shift;
}
static inline unsigned capture_payload_shift_for_depth(unsigned rate,unsigned samples)
{
    unsigned shift=capture_payload_shift(rate),pairs=rate==200000000?4u:2u;
    if(samples)while(shift && (pairs<<(shift-1))>=samples)--shift;
    return shift;
}
/* Power bins: 0..63 in single-code-squared steps, then 5-bit mantissa with
   binary scale. 255 is reserved for the uncalibrated 17-point mean. */
static inline unsigned capture_probe_power_code(double power)
{
    unsigned value,scale=0;
    if(!(power>=0 && power<4032))return 256;
    value=(unsigned)power;
    while(value>=64){value>>=1;++scale;}
    return value<32?value:((scale+1)<<5)|(value-32);
}
#endif
