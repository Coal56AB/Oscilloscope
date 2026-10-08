#ifndef CAPTURE_ADAPTER_H
#define CAPTURE_ADAPTER_H
#include "capture_pipeline.h"
#include "demo_signal.h"
void capture_request_from_demo(const DemoSignal *demo, CaptureRequest *request);
void capture_display_apply(DemoSignal *demo, const DisplayFrame *frame);
#endif
