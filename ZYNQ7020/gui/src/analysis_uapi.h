#ifndef ANALYSIS_UAPI_H
#define ANALYSIS_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define OSC_ANALYSIS_ABI 1u
#define OSC_ANALYSIS_MAX_SAMPLES 1000000u
#define OSC_ANALYSIS_BYTES 1u
#define OSC_ANALYSIS_VALUES 2u
#define OSC_ANALYSIS_AMPLITUDE_ONLY 4u
#define OSC_ANALYSIS_ASYNC 8u
struct osc_analysis_info { __u32 abi_version,max_samples,features,reserved; };
/* format: 0 = byte codes, 1 = physical float values.
   Values, calibration and results are IEEE-754 binary32 bit patterns.
   Results: min, max, mean, RMS, frequency Hz, duty %, clipped, completed (1).
   A zero sample rate requests amplitude statistics without timing analysis.
   Input arrays remain owned by the caller until the synchronous ioctl ends. */
struct osc_analysis_measure {
    __aligned_u64 codes_address,values_address;
    __u32 samples,stride,format,gain_bits,zero_bits,sample_rate_bits;
    __u32 result_bits[8],reserved[2];
};
struct osc_analysis_result { __u32 bits[8]; };
#define OSC_ANALYSIS_GET_INFO _IOR('P',0,struct osc_analysis_info)
#define OSC_ANALYSIS_MEASURE _IOWR('P',1,struct osc_analysis_measure)
/* BEGIN copies input before returning. The descriptor owns one pending job
   until FINISH or close; a second BEGIN returns EBUSY even after its IRQ. */
#define OSC_ANALYSIS_BEGIN _IOW('P',2,struct osc_analysis_measure)
#define OSC_ANALYSIS_FINISH _IOR('P',3,struct osc_analysis_result)
#endif
