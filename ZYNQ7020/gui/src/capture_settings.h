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
#endif
