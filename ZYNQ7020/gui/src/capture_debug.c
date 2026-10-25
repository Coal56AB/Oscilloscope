#include "capture_debug.h"
#include <math.h>
#include <string.h>
/* Range minima/maxima of the periodic generator table. Sparse tables keep
   peak synthesis O(samples), including when one interval spans many cycles. */
static int add_peaks(const CaptureBuffer *input,CaptureBuffer *output,
                     const CaptureDebug *debug,const uint8_t table[2][1024])
{
    uint8_t low[2][11][1024],high[2][11][1024];
    uint64_t fs=input->sample_rate_hz, advance=(uint64_t)debug->frequency_hz*2;
    uint64_t start=(((input->first_sample%fs)*(debug->frequency_hz%fs))%fs)<<32;
    uint64_t phase_advance=(advance%fs)<<32;
    uint64_t phase_step=phase_advance/fs,rem_step=phase_advance%fs,rem=start%fs;
    uint32_t phase=(uint32_t)(start/fs);
    unsigned width=advance>=fs?1024:(unsigned)((advance*1024+fs-1)/fs)+1,level=0,ch,k,i;
    size_t group;
    if(!output->peaks)return 1;
    if(width>1024)width=1024;
    while((2u<<level)<=width)++level;
    for(ch=0;ch<2;++ch)if(debug->wave[ch]!=SIGNAL_WAVE_NOISE){
        memcpy(low[ch][0],table[ch],1024);memcpy(high[ch][0],table[ch],1024);
        for(k=1;k<=level;++k)for(i=0;i<1024;++i){
            unsigned next=(i+(1u<<(k-1)))&1023;
            low[ch][k][i]=low[ch][k-1][i]<low[ch][k-1][next]?low[ch][k-1][i]:low[ch][k-1][next];
            high[ch][k][i]=high[ch][k-1][i]>high[ch][k-1][next]?high[ch][k-1][i]:high[ch][k-1][next];
        }
    }
    for(group=0;group<output->bytes/4;++group){
        unsigned a=phase>>22,b=(a+width-(1u<<level))&1023;
        for(ch=0;ch<2;++ch){
            unsigned lo,hi;
            if(debug->wave[ch]==SIGNAL_WAVE_NOISE){
                unsigned u=output->data[4*group+ch],v=output->data[4*group+2+ch];
                lo=u<v?u:v;hi=u>v?u:v;
            }else{
                lo=low[ch][level][a]<low[ch][level][b]?low[ch][level][a]:low[ch][level][b];
                hi=high[ch][level][a]>high[ch][level][b]?high[ch][level][a]:high[ch][level][b];
                if(lo)--lo;if(hi<255)++hi;
            }
            output->peaks[4*group+ch]=(uint8_t)lo;
            output->peaks[4*group+2+ch]=(uint8_t)hi;
        }
        phase+=(uint32_t)phase_step;rem+=rem_step;
        if(rem>=fs){++phase;rem-=fs;}
    }
    return 1;
}
static int add_noise(const CaptureBuffer *input, CaptureBuffer *output,
                     const CaptureDebug *debug,const uint8_t table[2][1024])
{
    uint8_t channels[4];
    uint32_t mask;
    size_t i;
    channels[0] = channels[2] = debug->wave[0] != SIGNAL_WAVE_NOISE;
    channels[1] = channels[3] = debug->wave[1] != SIGNAL_WAVE_NOISE;
    memcpy(&mask, channels, sizeof(mask));
    if (!mask) return add_peaks(input,output,debug,table);
    /* The periodic shapes occupy codes 48..208. Adding independent -1/0/+1
       code noise in each byte cannot carry or borrow into the next channel.
       Apply it after the periodic copies so cycles do not repeat the noise. */
    for (i = 0; i + 4 <= output->bytes; i += 4) {
        uint32_t codes;
        uint32_t noise = generator_noise_code((uint32_t)input->sequence,
            (size_t)(input->first_sample + i / 2), 0);
        memcpy(&codes, output->data + i, sizeof(codes));
        codes += noise & mask;
        codes -= (noise >> 1) & mask;
        memcpy(output->data + i, &codes, sizeof(codes));
    }
    for (; i < output->bytes; ++i) {
        uint32_t noise = generator_noise_code((uint32_t)input->sequence,
            (size_t)(input->first_sample + i / 2), (int)(i % 2));
        if (channels[i % 2])
            output->data[i] = (uint8_t)(output->data[i] + (int)(noise & 1) - (int)((noise >> 1) & 1));
    }
    return add_peaks(input,output,debug,table);
}
int capture_debug_replace(const CaptureBuffer *input, CaptureBuffer *output,
                           const CaptureDebug *debug)
{
    uint8_t *storage,*peak_storage;
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
            double amplitude=debug->amplitude[ch]>0?debug->amplitude[ch]:1;
            double code = 128 + (value - debug->offset_fraction[ch]) * (ch ? 80 : 60)*amplitude;
            table[ch][i] = (uint8_t)(code < 0 ? 0 : code > 255 ? 255 : code);
        }
    }
    storage = output->data; peak_storage=output->peaks; capacity = output->capacity;
    *output = *input;
    output->data = storage; output->capacity = capacity;
    output->peaks=input->peaks?peak_storage:NULL;
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
            return add_noise(input, output, debug,table);
        }
        for (i = 0; i < input->bytes / 2; ++i) {
            storage[2 * i] = table[0][phase >> 22];
            storage[2 * i + 1] = table[1][phase >> 22];
            phase += step;
            residue += step_residue;
            if (residue >= input->sample_rate_hz) { ++phase; residue -= input->sample_rate_hz; }
        }
        return add_noise(input, output, debug,table);
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
    return add_noise(input, output, debug,table);
}
