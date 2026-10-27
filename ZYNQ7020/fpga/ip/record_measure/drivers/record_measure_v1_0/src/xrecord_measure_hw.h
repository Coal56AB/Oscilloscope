// ==============================================================
// Vivado(TM) HLS - High-Level Synthesis from C, C++ and SystemC v2019.1 (64-bit)
// Copyright 1986-2019 Xilinx, Inc. All Rights Reserved.
// ==============================================================
// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read)
//        bit 7  - auto_restart (Read/Write)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0  - Channel 0 (ap_done)
//        bit 1  - Channel 1 (ap_ready)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0  - Channel 0 (ap_done)
//        bit 1  - Channel 1 (ap_ready)
//        others - reserved
// 0x10 : Data signal of codes
//        bit 31~0 - codes[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of values
//        bit 31~0 - values[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of result
//        bit 31~0 - result[31:0] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of count
//        bit 31~0 - count[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of stride
//        bit 31~0 - stride[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of filtered
//        bit 31~0 - filtered[31:0] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of gain
//        bit 31~0 - gain[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of zero
//        bit 31~0 - zero[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of sample_rate
//        bit 31~0 - sample_rate[31:0] (Read/Write)
// 0x54 : reserved
// 0x58 : Data signal of operation
//        bit 31~0 - operation[31:0] (Read/Write)
// 0x5c : reserved
// 0x60 : Data signal of filter_level
//        bit 31~0 - filter_level[31:0] (Read/Write)
// 0x64 : reserved
// 0x68 : Data signal of noise_rms
//        bit 31~0 - noise_rms[31:0] (Read/Write)
// 0x6c : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL           0x00
#define XRECORD_MEASURE_CONTROL_ADDR_GIE               0x04
#define XRECORD_MEASURE_CONTROL_ADDR_IER               0x08
#define XRECORD_MEASURE_CONTROL_ADDR_ISR               0x0c
#define XRECORD_MEASURE_CONTROL_ADDR_CODES_DATA        0x10
#define XRECORD_MEASURE_CONTROL_BITS_CODES_DATA        32
#define XRECORD_MEASURE_CONTROL_ADDR_VALUES_DATA       0x18
#define XRECORD_MEASURE_CONTROL_BITS_VALUES_DATA       32
#define XRECORD_MEASURE_CONTROL_ADDR_RESULT_DATA       0x20
#define XRECORD_MEASURE_CONTROL_BITS_RESULT_DATA       32
#define XRECORD_MEASURE_CONTROL_ADDR_COUNT_DATA        0x28
#define XRECORD_MEASURE_CONTROL_BITS_COUNT_DATA        32
#define XRECORD_MEASURE_CONTROL_ADDR_STRIDE_DATA       0x30
#define XRECORD_MEASURE_CONTROL_BITS_STRIDE_DATA       32
#define XRECORD_MEASURE_CONTROL_ADDR_FILTERED_DATA     0x38
#define XRECORD_MEASURE_CONTROL_BITS_FILTERED_DATA     32
#define XRECORD_MEASURE_CONTROL_ADDR_GAIN_DATA         0x40
#define XRECORD_MEASURE_CONTROL_BITS_GAIN_DATA         32
#define XRECORD_MEASURE_CONTROL_ADDR_ZERO_DATA         0x48
#define XRECORD_MEASURE_CONTROL_BITS_ZERO_DATA         32
#define XRECORD_MEASURE_CONTROL_ADDR_SAMPLE_RATE_DATA  0x50
#define XRECORD_MEASURE_CONTROL_BITS_SAMPLE_RATE_DATA  32
#define XRECORD_MEASURE_CONTROL_ADDR_OPERATION_DATA    0x58
#define XRECORD_MEASURE_CONTROL_BITS_OPERATION_DATA    32
#define XRECORD_MEASURE_CONTROL_ADDR_FILTER_LEVEL_DATA 0x60
#define XRECORD_MEASURE_CONTROL_BITS_FILTER_LEVEL_DATA 32
#define XRECORD_MEASURE_CONTROL_ADDR_NOISE_RMS_DATA    0x68
#define XRECORD_MEASURE_CONTROL_BITS_NOISE_RMS_DATA    32

