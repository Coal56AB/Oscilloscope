#ifndef CAPTURE_PIPELINE_H
#define CAPTURE_PIPELINE_H
#include "capture_processor.h"
#include "capture_debug.h"
#include "input_range.h"
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
    int density_enabled;
    unsigned density_rows;
    unsigned hidden_channel_mask;
    unsigned filter_level[2];
    ChannelResponse response[2];
    int response_enabled[2];
    unsigned calibration_mask;
    unsigned baseline_mask;
    ChannelBaseline baseline[2][INPUT_RANGE_STAGE_COUNT];
    unsigned range_stage[2];
} CaptureRequest;
typedef struct {
    uint64_t blocks, bytes, dropped, overwritten, display_replaced, processing_ns, waveforms;
    uint64_t triggered_ns; /* Source accepted a record; processing may still be busy. */
    uint64_t acquisition_ns, search_ns, search_max_ns; /* Cumulative hardware-stage times. */
    uint64_t rejected_blocks; /* Quiet payloads rejected by hardware bounds. */
    uint64_t deadtime_blocks; /* Input drained while the accepted record is presented or in holdoff. */
    uint64_t probe_blocks; /* Headers with a matching hardware filter/level probe. */
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
/* Single consumer; copies a completed immutable slot without holding settings. */
int capture_pipeline_frame(CapturePipeline *pipeline, DisplayFrame *frame);
/* Single GUI consumer: lease remains immutable until the next successful
   acquire or pipeline destruction. Choose either this API or frame copying. */
const DisplayFrame *capture_pipeline_acquire(CapturePipeline *pipeline);
void capture_pipeline_retain_frame(CapturePipeline *pipeline,const DisplayFrame *frame);
void capture_pipeline_release_frame(CapturePipeline *pipeline,const DisplayFrame *frame);
/* Opt into first-trigger presentation: acknowledge an actually displayed
   capture_id (zero enables it before the first frame). DMA keeps draining. */
void capture_pipeline_presented(CapturePipeline *pipeline,uint64_t capture_id);
void capture_pipeline_metrics(CapturePipeline *pipeline, CaptureMetrics *metrics);
#endif
