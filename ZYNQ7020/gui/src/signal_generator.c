#include "signal_generator.h"
#include <math.h>
#define PI 3.14159265358979323846
uint32_t generator_noise_code(uint32_t sequence, size_t x, int channel)
{
    uint32_t value = (uint32_t)x * 0x9e3779b9u ^
                     sequence * 0x85ebca6bu ^
                     (uint32_t)(channel + 1) * 0xc2b2ae35u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

double generator_noise(uint32_t sequence, size_t x, int channel)
{
    return (generator_noise_code(sequence, x, channel) & 0xffffu) / 65535.0;
}

double generator_value(int shape, double t, double period,
                              uint32_t sequence, size_t x, int channel)
{
    double p = fmod(t, period) / period;
    if (p < 0.0) p += 1.0;
    switch (shape) {
        case SIGNAL_WAVE_SQUARE:
            if (p < 0.02) return p / 0.02;
            if (p < 0.50) return 1.0;
            if (p < 0.52) return (0.52 - p) / 0.02;
            return 0.0;
        case SIGNAL_WAVE_SINE:
            return 0.5 + 0.5 * sin(2.0 * PI * p);
        case SIGNAL_WAVE_TRIANGLE:
            return p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0;
        case SIGNAL_WAVE_SAW:
            return p;
        case SIGNAL_WAVE_SINC: {
            double z = (p - 0.5) * 8.0 * PI;
            double sinc = fabs(z) < 1e-9 ? 1.0 : sin(z) / z;
            return 0.5 + 0.5 * sinc;
        }
        case SIGNAL_WAVE_NOISE:
            return generator_noise(sequence, x, channel);
        default:
            return 0.0;
    }
}
