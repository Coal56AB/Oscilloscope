#ifndef CHANNEL_RESPONSE_H
#define CHANNEL_RESPONSE_H
#include <stddef.h>
#define RESPONSE_POINTS 96
enum { CHANNEL_ZERO_VALID=1, CHANNEL_NOISE_VALID=2 };
typedef struct {
    int valid; /* Independent ZERO/NOISE flags. */
    double zero,noise_rms,volts_per_code,noise_volts_per_code;
} ChannelBaseline;
int channel_baseline_valid(const ChannelBaseline *baseline);
/* Voltage correction only for the measured ADC/probe gain. */
double channel_baseline_zero(const ChannelBaseline *baseline,double gain);
/* Correct coherent sine amplitude for independent background noise in a Hann
   FFT bin. Reject records whose added AC power is below the noise uncertainty. */
double channel_baseline_amplitude(const ChannelBaseline *baseline,double gain,unsigned fft_size,
                                  double amplitude,double mean,double rms);
typedef struct {
    unsigned count;
    double reference;
    double frequency[RESPONSE_POINTS],db[RESPONSE_POINTS];
} ChannelResponse;
int channel_response_valid(const ChannelResponse *curve);
/* Constant input amplitude: first valid sine point establishes 0 dB. */
int channel_response_add(ChannelResponse *curve,double frequency,double amplitude);
double channel_response_db(const ChannelResponse *curve,double frequency);
double channel_response_gain(const ChannelResponse *curve,double frequency);
double channel_response_demo_gain(unsigned channel,double frequency);
/* Magnitude-only correction of a derived record, with reflected end padding.
   Frequencies outside the measured interval are left uncorrected. */
int channel_response_correct(double *samples,size_t count,double sample_rate,const ChannelResponse *curve);
#endif
