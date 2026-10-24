#ifndef BATTERY_LEVEL_H
#define BATTERY_LEVEL_H

/* Provisional single-cell Li-ion voltage estimate, not a capacity measurement.
   Input is the battery voltage before the board's 1:2 divider, in mV. */
static inline unsigned battery_level_percent(unsigned millivolts)
{
    static const unsigned voltage[] = {3300,3600,3700,3850,4000,4200};
    static const unsigned percent[] = {0,10,25,60,85,100};
    unsigned i;
    if (millivolts <= voltage[0]) return 0;
    for (i=1;i<sizeof(voltage)/sizeof(voltage[0]);++i)
        if (millivolts < voltage[i]) {
            unsigned width=voltage[i]-voltage[i-1];
            return percent[i-1]+((millivolts-voltage[i-1])*(percent[i]-percent[i-1])+width/2)/width;
        }
    return 100;
}
#endif
