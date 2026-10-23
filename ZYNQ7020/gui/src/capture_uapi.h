#ifndef CAPTURE_UAPI_H
#define CAPTURE_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define OSC_CAPTURE_ABI 3
#define OSC_CAPTURE_SAMPLE_FORMAT 1
/* Fixed-width ABI shared by the dmaengine driver and userspace. */
#define OSC_CAPTURE_DISCONTINUITY 1u
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
#include "capture_rates.h"
#define OSC_CAPTURE_CONFIGURE _IOW('O', 0x13, struct osc_capture_config)
#define OSC_CAPTURE_GET_INFO _IOR('O', 0x10, struct osc_capture_info)
#define OSC_CAPTURE_DEQUEUE _IOR('O', 0x11, struct osc_capture_block)
#define OSC_CAPTURE_RELEASE _IOW('O', 0x12, __u32)
#endif
