#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H
#include <stddef.h>
#include <stdint.h>
enum {
    SIGNAL_WAVE_SQUARE, SIGNAL_WAVE_SINE, SIGNAL_WAVE_TRIANGLE,
    SIGNAL_WAVE_SAW, SIGNAL_WAVE_SINC, SIGNAL_WAVE_NOISE, SIGNAL_WAVE_COUNT
};
uint32_t generator_noise_code(uint32_t sequence, size_t sample, int channel);
double generator_noise(uint32_t sequence, size_t sample, int channel);
double generator_value(int shape, double time, double period,
                       uint32_t sequence, size_t sample, int channel);
#endif
