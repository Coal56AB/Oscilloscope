#ifndef CAPTURE_HISTORY_H
#define CAPTURE_HISTORY_H
#include "capture.h"
typedef struct {
    uint8_t *data;
    uint8_t *peaks;
    size_t capacity, count;
    CaptureBuffer latest;
} CaptureHistory;
void capture_history_init(CaptureHistory *h, uint8_t *data, uint8_t *peaks, size_t samples);
void capture_history_append(CaptureHistory *h, const CaptureBuffer *b);
/* Reserve the next block's exact sample count in the linear history. The
   producer writes there, then calls append with the resulting metadata.
   Metadata gaps are discarded before reservation; history holds no DMA lease. */
int capture_history_prepare(CaptureHistory *h,const CaptureBuffer *metadata,int has_peaks,uint8_t **data,uint8_t **peaks);
/* Borrow a contiguous immutable view, valid until the next prepare/append/reset. */
int capture_history_view(const CaptureHistory *h,CaptureBuffer *b);
int capture_history_snapshot(const CaptureHistory *h, CaptureBuffer *b);
#endif
