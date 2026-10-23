#include "capture_debug.h"
#include <math.h>
#include <string.h>
int capture_debug_replace(const CaptureBuffer *input, CaptureBuffer *output,
                           const CaptureDebug *debug)
{
    uint8_t *storage;
    size_t capacity, i;
    unsigned ch;
    uint8_t table[2][1024];
    uint32_t phase, step;
    uint64_t period, residue, step_residue;
    if (!capture_buffer_valid(input) || !output || !debug || !debug->enabled ||
        !debug->frequency_hz ||
        !output->data || output->capacity < input->bytes || output->data == input->data)
        return 0;
    for (ch = 0; ch < 2; ++ch) {
        if (debug->wave[ch] < 0 || debug->wave[ch] >= SIGNAL_WAVE_COUNT ||
            !isfinite(debug->offset_fraction[ch]) || debug->offset_fraction[ch] < 0 ||
            debug->offset_fraction[ch] > 1) return 0;
        if (debug->wave[ch] == SIGNAL_WAVE_NOISE) continue;
        for (i = 0; i < 1024; ++i) {
            double value = generator_value(debug->wave[ch], i / 1024.0 + ch * 0.2,
                                             1.0, (uint32_t)input->sequence, i, ch);
            double code = 128 + (value - debug->offset_fraction[ch]) * (ch ? 80 : 60);
            table[ch][i] = (uint8_t)(code < 0 ? 0 : code > 255 ? 255 : code);
        }
    }
    storage = output->data; capacity = output->capacity;
    *output = *input;
    output->data = storage; output->capacity = capacity;
    output->volts_per_code[0] = output->volts_per_code[1] = 0.02;
    output->zero_code[0] = output->zero_code[1] = 128;
    {
        uint64_t a = input->sample_rate_hz, b = debug->frequency_hz;
        while (b) { uint64_t remainder = a % b; a = b; b = remainder; }
        period = input->sample_rate_hz / a;
        uint64_t initial = (((input->first_sample % input->sample_rate_hz) *
            (debug->frequency_hz % input->sample_rate_hz)) % input->sample_rate_hz) << 32;
        uint64_t advance = ((uint64_t)debug->frequency_hz % input->sample_rate_hz) << 32;
        phase = (uint32_t)(initial / input->sample_rate_hz);
        residue = initial % input->sample_rate_hz;
        step = (uint32_t)(advance / input->sample_rate_hz);
        step_residue = advance % input->sample_rate_hz;
    }
    /* This runs only after a real DMA completion. Reading and discarding every
       byte of the uncached DMA mapping adds bus traffic, not ADC timing. */
    if (debug->wave[0] != SIGNAL_WAVE_NOISE && debug->wave[1] != SIGNAL_WAVE_NOISE) {
        if (period <= input->bytes / 4) {
            size_t filled;
            for (i = 0; i < period; ++i) {
                storage[2 * i] = table[0][phase >> 22];
                storage[2 * i + 1] = table[1][phase >> 22];
                phase += step;
                residue += step_residue;
                if (residue >= input->sample_rate_hz) { ++phase; residue -= input->sample_rate_hz; }
            }
            /* Periodic DEBUG data repeats; do not synthesize a million
               identical cycles sample by sample. DMA still supplies each block. */
            filled = (size_t)period * 2;
            while (filled < input->bytes) {
                size_t bytes = input->bytes - filled;
                if (bytes > filled) bytes = filled;
                memcpy(storage + filled, storage, bytes);
                filled += bytes;
            }
            return 1;
        }
        for (i = 0; i < input->bytes / 2; ++i) {
            storage[2 * i] = table[0][phase >> 22];
            storage[2 * i + 1] = table[1][phase >> 22];
            phase += step;
            residue += step_residue;
            if (residue >= input->sample_rate_hz) { ++phase; residue -= input->sample_rate_hz; }
        }
        return 1;
    }
    {
        unsigned base0 = (unsigned)(128 - debug->offset_fraction[0] * 60);
        unsigned base1 = (unsigned)(128 - debug->offset_fraction[1] * 80);
        size_t count = input->bytes / 2, first_sample = (size_t)input->first_sample;
        uint32_t sequence = (uint32_t)input->sequence;
        int noise0 = debug->wave[0] == SIGNAL_WAVE_NOISE;
        int noise1 = debug->wave[1] == SIGNAL_WAVE_NOISE;
        for (i = 0; i < count; ++i) {
            storage[2 * i] = noise0 ? base0 + generator_noise_code(sequence,
                first_sample + i, 0) % 61 : table[0][phase >> 22];
            storage[2 * i + 1] = noise1 ? base1 + generator_noise_code(sequence,
                first_sample + i, 1) % 81 : table[1][phase >> 22];
            if (noise0 && noise1) continue;
            phase += step;
            residue += step_residue;
            if (residue >= input->sample_rate_hz) { ++phase; residue -= input->sample_rate_hz; }
        }
    }
    return 1;
}
