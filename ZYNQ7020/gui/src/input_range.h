#ifndef INPUT_RANGE_H
#define INPUT_RANGE_H
#include "control_protocol.h"

/* DiffMeasure.SchDoc: a high output opens the NC shunt of its resistor. */
static inline unsigned input_range_relays(unsigned index)
{
    static const unsigned char masks[CONTROL_SCALE_COUNT] = {7,7,6,6,6,4};
    return index < CONTROL_SCALE_COUNT ? masks[index] : 0;
}

/* Nominal input volts/code, before the probe: 1.024 Vpp / 256 codes,
   a 1 Mohm series arm and unity differential gain of the AD8138.
   The selected relay stage is independent of the screen's volts/div. */
static inline double input_range_volts_per_code(unsigned index)
{
    unsigned mask = input_range_relays(index);
    double lower;
    if (!mask) return 0;
    lower = 510 + ((mask & 1) ? 910000 : 0) +
                  ((mask & 2) ? 47000 : 0) + ((mask & 4) ? 4700 : 0);
    return 0.004 * (1000000 + lower) / lower;
}
#endif
