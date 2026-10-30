#ifndef INPUT_RANGE_H
#define INPUT_RANGE_H
#include "control_protocol.h"
#define INPUT_RANGE_STAGE_COUNT 3
#define INPUT_PROBE_COUNT 3
/* Stored probe index: 0 = x1, 1 = x10, 2 = x100. */
static inline unsigned input_probe_multiplier(unsigned index)
{
    static const unsigned factors[INPUT_PROBE_COUNT]={1,10,100};
    return index<INPUT_PROBE_COUNT?factors[index]:1;
}
static inline unsigned input_range_stage(unsigned index)
{
    return index<2?0:index<5?1:2;
}
static inline unsigned input_range_stage_scale(unsigned stage)
{
    static const unsigned indices[INPUT_RANGE_STAGE_COUNT]={0,2,5};
    return stage<INPUT_RANGE_STAGE_COUNT?indices[stage]:0;
}

/* DiffMeasure.SchDoc: a high output opens the NC shunt of its resistor. */
static inline unsigned input_range_relays(unsigned index)
{
    static const unsigned char masks[CONTROL_SCALE_COUNT] = {7,7,6,6,6,4};
    return index < CONTROL_SCALE_COUNT ? masks[index] : 0;
}

/* Nominal input volts/code, before the probe: 1.024 Vpp / 256 codes,
   a 1 Mohm series arm and unity differential gain of the AD8138.
   The selected relay stage is independent of the screen's volts/div. */
static inline double input_range_mask_volts_per_code(unsigned mask)
{
    double lower;
    lower = 510 + ((mask & 1) ? 910000 : 0) +
                  ((mask & 2) ? 47000 : 0) + ((mask & 4) ? 4700 : 0);
    return 0.004 * (1000000 + lower) / lower;
}
static inline double input_range_volts_per_code(unsigned index)
{
    return index < CONTROL_SCALE_COUNT ? input_range_mask_volts_per_code(input_range_relays(index)) : 0;
}
#endif
