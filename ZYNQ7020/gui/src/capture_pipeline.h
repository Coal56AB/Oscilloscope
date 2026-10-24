#ifndef CAPTURE_PIPELINE_H
#define CAPTURE_PIPELINE_H
#include "capture_processor.h"
#include "capture_debug.h"
typedef struct CapturePipeline CapturePipeline;
typedef struct {
    double duration_seconds, pan_fraction;
    double zero_y[2], pixels_per_volt[2];
    int running;
    CaptureDebug debug;
    double adc_volts_per_code[2], trigger_voltage;
    int trigger_mode, trigger_channel, trigger_edge;
    uint64_t holdoff_samples, force_sequence;
    double overview_duration_seconds, zoom_offset_seconds;
    int spectrum_enabled;
    unsigned record_samples;
    int roll;
} CaptureRequest;
typedef struct {
    uint64_t blocks, bytes, dropped, overwritten, display_replaced, processing_ns, waveforms;
    size_t occupancy, peak_occupancy;
    int source_error;
    int waiting;
    uint64_t adc_age_ms;
    uint8_t adc_code[2];
    int adc_valid;
} CaptureMetrics;
/* NULL file = raw synthetic source. File source is read in acquisition worker. */
CapturePipeline *capture_pipeline_create(const char *file);
#ifdef SCOPE_ENABLE_ZYNQ
CapturePipeline *capture_pipeline_create_zynq(const char *device,
    const double volts_per_code[2], const double zero_code[2]);
#endif
void capture_pipeline_destroy(CapturePipeline *pipeline);
void capture_pipeline_request(CapturePipeline *pipeline, const CaptureRequest *request);
int capture_pipeline_frame(CapturePipeline *pipeline, DisplayFrame *frame);
void capture_pipeline_metrics(CapturePipeline *pipeline, CaptureMetrics *metrics);
#endif
