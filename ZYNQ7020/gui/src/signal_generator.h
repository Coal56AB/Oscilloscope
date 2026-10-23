#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H
#include <stddef.h>
#include <stdint.h>
enum {
    SIGNAL_WAVE_SQUARE, SIGNAL_WAVE_SINE, SIGNAL_WAVE_TRIANGLE,
    SIGNAL_WAVE_SAW, SIGNAL_WAVE_SINC, SIGNAL_WAVE_NOISE, SIGNAL_WAVE_COUNT
};
static inline uint32_t generator_noise_code(uint32_t sequence, size_t sample, int channel)
{
    uint32_t value = (uint32_t)sample * 0x9e3779b9u ^
                     sequence * 0x85ebca6bu ^
                     (uint32_t)(channel + 1) * 0xc2b2ae35u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}
double generator_noise(uint32_t sequence, size_t sample, int channel);
double generator_value(int shape, double time, double period,
                       uint32_t sequence, size_t sample, int channel);
#endif
