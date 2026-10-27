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
/* Optional necessary-side probe: level[7:0], noise-power bin[15:8],
   channel[16], enable[17]. Applied by the next CONFIGURE, never mid-block. */
struct osc_capture_probe { __u32 control; };
#define OSC_CAPTURE_PROBE_ENABLE (1u<<17)
#define OSC_CAPTURE_PROBE_MASK ((1u<<18)-1)
/* Atomic hardware DSP configuration. A new arm_id admits one trigger;
   minimum_sample/holdoff_samples are in effective samples of the stream. */
struct osc_capture_dsp {
    __u32 control,noise_power;
    __s32 level;
    __u32 hysteresis;
    __aligned_u64 minimum_sample,holdoff_samples;
    __u32 arm_id,reserved;
};
#define OSC_CAPTURE_DSP_ENABLE 1u
#define OSC_CAPTURE_DSP_CHANNEL_SHIFT 1
#define OSC_CAPTURE_DSP_EDGE_SHIFT 2
#define OSC_CAPTURE_DSP_FILTER_SHIFT 4
#define OSC_CAPTURE_DSP_MASK 63u
#include "capture_rates.h"
#define OSC_CAPTURE_CONFIGURE _IOW('O', 0x13, struct osc_capture_config)
#define OSC_CAPTURE_TEST_CLOCK _IOW('O', 0x14, struct osc_capture_test_clock)
#define OSC_CAPTURE_SET_PROBE _IOW('O', 0x15, struct osc_capture_probe)
#define OSC_CAPTURE_SET_DSP _IOW('O', 0x16, struct osc_capture_dsp)
#define OSC_CAPTURE_GET_INFO _IOR('O', 0x10, struct osc_capture_info)
#define OSC_CAPTURE_DEQUEUE _IOR('O', 0x11, struct osc_capture_block)
#define OSC_CAPTURE_RELEASE _IOW('O', 0x12, __u32)
#endif
