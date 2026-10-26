#include "capture_history.h"
#include <string.h>
void capture_history_init(CaptureHistory *h, uint8_t *data, uint8_t *peaks, size_t samples)
{
    memset(h,0,sizeof(*h));h->data=data;h->peaks=peaks;h->capacity=samples;
}
static void discard_gap(CaptureHistory *h,const CaptureBuffer *b,int peaks)
{
    if(h->count && (b->first_sample!=h->latest.first_sample+h->latest.bytes/2 ||
        b->sample_rate_hz!=h->latest.sample_rate_hz || !!peaks!=!!h->latest.peaks))h->count=0;
}
int capture_history_prepare(CaptureHistory *h,const CaptureBuffer *b,int has_peaks,uint8_t **data,uint8_t **peaks)
{
    size_t samples=b->bytes/2;
    if(!samples || samples>h->capacity || !data || !peaks ||
       (has_peaks && (!h->peaks || (h->capacity&1))))return 0;
    discard_gap(h,b,has_peaks);
    size_t keep=h->count<h->capacity-samples?h->count:h->capacity-samples;
    memmove(h->data,h->data+(h->count-keep)*2,keep*2);
    if(h->peaks)memmove(h->peaks,h->peaks+(h->count-keep)*2,keep*2);
    h->count=keep;*data=h->data+keep*2;*peaks=h->peaks?h->peaks+keep*2:NULL;
    return 1;
}
void capture_history_append(CaptureHistory *h, const CaptureBuffer *b)
{
    size_t count=b->bytes/2,offset=0,keep;
    if(!h->capacity || !capture_buffer_valid(b))return;
    if(b->peaks && (!h->peaks || h->capacity%2))return;
    /* Never join samples across an acquisition gap or a rate change. */
    discard_gap(h,b,b->peaks!=NULL);
    if(count>h->capacity){offset=count-h->capacity;count=h->capacity;}
    keep=h->count<h->capacity-count?h->count:h->capacity-count;
    /* Keep a linear view so trigger search can borrow history directly. Only
       the retained prefix moves; accepted records are copied separately. */
    memmove(h->data,h->data+(h->count-keep)*2,keep*2);
    if(h->data+keep*2!=b->data+offset*2)memmove(h->data+keep*2,b->data+offset*2,count*2);
    if(b->peaks){
        memmove(h->peaks,h->peaks+(h->count-keep)*2,keep*2);
        if(h->peaks+keep*2!=b->peaks+offset*2)memmove(h->peaks+keep*2,b->peaks+offset*2,count*2);
    }
    h->count=keep+count;h->latest=*b;
}
int capture_history_view(const CaptureHistory *h,CaptureBuffer *b)
{
    if(!h->count)return 0;
    *b=h->latest;b->data=h->data;b->capacity=h->capacity*2;b->bytes=h->count*2;
    b->peaks=h->latest.peaks?h->peaks:NULL;
    b->first_sample=h->latest.first_sample+h->latest.bytes/2-h->count;
    b->trigger_sample=CAPTURE_NO_TRIGGER;
    return 1;
}
int capture_history_snapshot(const CaptureHistory *h, CaptureBuffer *b)
{
    CaptureBuffer view;
    uint8_t *data=b->data,*peaks=b->peaks;size_t capacity=b->capacity;
    if(!capture_history_view(h,&view) || capacity<view.bytes || (view.peaks && !peaks))return 0;
    memcpy(data,view.data,view.bytes);
    if(view.peaks)memcpy(peaks,view.peaks,view.bytes);
    *b=view;b->data=data;b->peaks=view.peaks?peaks:NULL;b->capacity=capacity;
    return 1;
}
