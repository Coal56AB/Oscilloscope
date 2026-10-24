#ifndef CAPTURE_UAPI_H
#define CAPTURE_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define OSC_CAPTURE_ABI 4
#define OSC_CAPTURE_REGISTER_VERSION 5
#define OSC_CAPTURE_SAMPLE_FORMAT 2
/* Fixed-width ABI shared by the dmaengine driver and userspace. */
#define OSC_CAPTURE_DISCONTINUITY 1u
/* Each 8-byte group: two CH1/CH2 samples, then CH1/CH2 min and max. */
#define OSC_CAPTURE_PEAKS 2u
struct osc_capture_info {
    __u32 abi_version, register_version, sample_format, bitstream_version;
    __u32 buffers, block_bytes;
    __aligned_u64 mapping_bytes;
};
struct osc_capture_block {
    __u32 index, bytes, flags, data_offset;
    __aligned_u64 sequence, sample_rate_hz, first_sample, trigger_sample, hardware_ns;
};
struct osc_capture_config { __u32 rate_index, payload_shift; };
/* Diagnostic physical encode clock = 100 MHz / 2^shift, both branches kept. */
struct osc_capture_test_clock { __u32 shift, payload_shift; };
#include "capture_rates.h"
#define OSC_CAPTURE_CONFIGURE _IOW('O', 0x13, struct osc_capture_config)
#define OSC_CAPTURE_TEST_CLOCK _IOW('O', 0x14, struct osc_capture_test_clock)
#define OSC_CAPTURE_GET_INFO _IOR('O', 0x10, struct osc_capture_info)
#define OSC_CAPTURE_DEQUEUE _IOR('O', 0x11, struct osc_capture_block)
#define OSC_CAPTURE_RELEASE _IOW('O', 0x12, __u32)
#endif
