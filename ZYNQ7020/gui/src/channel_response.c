#include "channel_response.h"
#include "dsp_fft.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int channel_response_valid(const ChannelResponse *c)
{
    unsigned i;
    if(!c || c->count>RESPONSE_POINTS || !isfinite(c->reference) || c->reference<0)return 0;
    if(c->count && c->reference<=0)return 0;
    for(i=0;i<c->count;++i)
        if(!isfinite(c->frequency[i]) || c->frequency[i]<=0 || c->frequency[i]>100000000 ||
           !isfinite(c->db[i]) || fabs(c->db[i])>80 || (i && c->frequency[i]<=c->frequency[i-1]))return 0;
    return 1;
}

int channel_response_add(ChannelResponse *c,double frequency,double amplitude)
{
    unsigned i,at=0;
    double db;
    if(!channel_response_valid(c) || !isfinite(frequency) || frequency<=0 || frequency>100000000 ||
       !isfinite(amplitude) || amplitude<=0)return 0;
    if(!c->count)c->reference=amplitude;
    db=20*log10(amplitude/c->reference);if(fabs(db)>80)return 0;
    for(i=0;i<c->count;++i){
        if(fabs(log(frequency/c->frequency[i]))<0.02){
            c->db[i]=0.75*c->db[i]+0.25*db;return 1;
        }
        if(c->frequency[i]<frequency)at=i+1;
    }
    if(c->count==RESPONSE_POINTS)return 0;
    memmove(c->frequency+at+1,c->frequency+at,(c->count-at)*sizeof(double));
    memmove(c->db+at+1,c->db+at,(c->count-at)*sizeof(double));
    c->frequency[at]=frequency;c->db[at]=db;++c->count;return 1;
}

double channel_response_db(const ChannelResponse *c,double frequency)
{
    unsigned i;
    if(!c || !c->count || !isfinite(frequency) || frequency<c->frequency[0] || frequency>c->frequency[c->count-1])return 0;
    for(i=1;i<c->count;++i)if(frequency<=c->frequency[i]){
        double fraction=log(frequency/c->frequency[i-1])/log(c->frequency[i]/c->frequency[i-1]);
        return c->db[i-1]+fraction*(c->db[i]-c->db[i-1]);
    }
    return c->db[c->count-1];
}

double channel_response_gain(const ChannelResponse *c,double frequency)
{
    double db=-channel_response_db(c,frequency);
    /* Bound noise amplification and excessive gain from an accidental bad point. */
    if(db>24)db=24;if(db<-24)db=-24;
    return pow(10,db/20);
}

double channel_response_demo_gain(unsigned channel,double frequency)
{
    double ratio=frequency/(channel?300000.0:100000.0);
    return 1/sqrt(1+ratio*ratio);
}

int channel_response_correct(double *samples,size_t count,double fs,const ChannelResponse *c)
{
    double *re,*im;size_t n=1,i,offset,period;
    if(!samples || count<2 || !isfinite(fs) || fs<=0 || !channel_response_valid(c) || c->count<2)return 0;
    if(count>1048576)return 0;
    while(n<2*count)n<<=1;
    re=calloc(2*n,sizeof(double));if(!re)return 0;im=re+n;
    offset=(n-count)/2;period=2*(count-1);
    for(i=0;i<n;++i){
        size_t k=(i+period-offset%period)%period;
        if(k>=count)k=period-k;
        re[i]=samples[k];
    }
    dsp_fft(re,im,n,0);
    for(i=0;i<=n/2;++i){
        double gain=channel_response_gain(c,(double)i*fs/n);
        re[i]*=gain;im[i]*=gain;
        if(i && i<n/2){re[n-i]*=gain;im[n-i]*=gain;}
    }
    dsp_fft(re,im,n,1);
    memcpy(samples,re+offset,count*sizeof(double));free(re);return 1;
}
