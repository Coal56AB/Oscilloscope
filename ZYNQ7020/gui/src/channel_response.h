#ifndef CHANNEL_RESPONSE_H
#define CHANNEL_RESPONSE_H
#include <stddef.h>
#define RESPONSE_POINTS 96
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
