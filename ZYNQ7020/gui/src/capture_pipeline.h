#ifndef CAPTURE_PIPELINE_H
#define CAPTURE_PIPELINE_H
#include "capture_processor.h"
typedef struct CapturePipeline CapturePipeline;
typedef struct {
    double duration_seconds, pan_fraction;
    double zero_y[2], pixels_per_volt[2];
    int running;
} CaptureRequest;
typedef struct {
    uint64_t blocks, bytes, dropped, overwritten, display_replaced, processing_ns;
    size_t occupancy, peak_occupancy;
    int source_error;
} CaptureMetrics;
/* NULL file = raw synthetic source. File source is read in acquisition worker. */
CapturePipeline *capture_pipeline_create(const char *file);
void capture_pipeline_destroy(CapturePipeline *pipeline);
void capture_pipeline_request(CapturePipeline *pipeline, const CaptureRequest *request);
int capture_pipeline_frame(CapturePipeline *pipeline, DisplayFrame *frame);
void capture_pipeline_metrics(CapturePipeline *pipeline, CaptureMetrics *metrics);
#endif
