#ifndef CAPTURE_HISTORY_H
#define CAPTURE_HISTORY_H
#include "capture.h"
typedef struct {
    uint8_t *data;
    size_t capacity, count, write;
    CaptureBuffer latest;
} CaptureHistory;
void capture_history_init(CaptureHistory *h, uint8_t *data, size_t samples);
void capture_history_append(CaptureHistory *h, const CaptureBuffer *b);
int capture_history_snapshot(const CaptureHistory *h, CaptureBuffer *b);
#endif
