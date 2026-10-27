#ifndef OSCILL_CAPTURE_INTERNAL_H
#define OSCILL_CAPTURE_INTERNAL_H
#include <linux/device.h>
#include <linux/types.h>
#define OSC_CAPTURE_ANALYSIS_PRESENT 4u
#define OSC_CAPTURE_ANALYSIS_AMPLITUDE_ONLY 8u
/* Read-only capability access; only the capture driver owns its GPIO region. */
u32 oscill_capture_status(struct device *device);
#endif
