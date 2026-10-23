#ifndef CAPTURE_RATES_H
#define CAPTURE_RATES_H
/* Rates are exact integer divisors of the continuously clocked 200 MSa/s ADC. */
#define OSC_CAPTURE_RATE_COUNT 20
static inline unsigned osc_capture_rate(unsigned index)
{
    unsigned rate = 200000000;
    if (index >= OSC_CAPTURE_RATE_COUNT) return 0;
    while (index >= 3) { rate /= 10; index -= 3; }
    return rate >> index;
}
#endif
