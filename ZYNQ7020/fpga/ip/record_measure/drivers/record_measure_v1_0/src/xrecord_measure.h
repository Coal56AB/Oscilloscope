// ==============================================================
// Vivado(TM) HLS - High-Level Synthesis from C, C++ and SystemC v2019.1 (64-bit)
// Copyright 1986-2019 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef XRECORD_MEASURE_H
#define XRECORD_MEASURE_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xrecord_measure_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#else
typedef struct {
    u16 DeviceId;
    u32 Control_BaseAddress;
} XRecord_measure_Config;
#endif

typedef struct {
    u32 Control_BaseAddress;
    u32 IsReady;
} XRecord_measure;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XRecord_measure_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XRecord_measure_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XRecord_measure_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XRecord_measure_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
int XRecord_measure_Initialize(XRecord_measure *InstancePtr, u16 DeviceId);
XRecord_measure_Config* XRecord_measure_LookupConfig(u16 DeviceId);
int XRecord_measure_CfgInitialize(XRecord_measure *InstancePtr, XRecord_measure_Config *ConfigPtr);
#else
int XRecord_measure_Initialize(XRecord_measure *InstancePtr, const char* InstanceName);
int XRecord_measure_Release(XRecord_measure *InstancePtr);
#endif

void XRecord_measure_Start(XRecord_measure *InstancePtr);
u32 XRecord_measure_IsDone(XRecord_measure *InstancePtr);
u32 XRecord_measure_IsIdle(XRecord_measure *InstancePtr);
u32 XRecord_measure_IsReady(XRecord_measure *InstancePtr);
void XRecord_measure_EnableAutoRestart(XRecord_measure *InstancePtr);
void XRecord_measure_DisableAutoRestart(XRecord_measure *InstancePtr);

void XRecord_measure_Set_codes(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_codes(XRecord_measure *InstancePtr);
void XRecord_measure_Set_values(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_values(XRecord_measure *InstancePtr);
void XRecord_measure_Set_result(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_result(XRecord_measure *InstancePtr);
void XRecord_measure_Set_count(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_count(XRecord_measure *InstancePtr);
void XRecord_measure_Set_stride(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_stride(XRecord_measure *InstancePtr);
void XRecord_measure_Set_filtered(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_filtered(XRecord_measure *InstancePtr);
void XRecord_measure_Set_gain(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_gain(XRecord_measure *InstancePtr);
void XRecord_measure_Set_zero(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_zero(XRecord_measure *InstancePtr);
void XRecord_measure_Set_sample_rate(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_sample_rate(XRecord_measure *InstancePtr);
void XRecord_measure_Set_operation(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_operation(XRecord_measure *InstancePtr);
void XRecord_measure_Set_filter_level(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_filter_level(XRecord_measure *InstancePtr);
void XRecord_measure_Set_noise_rms(XRecord_measure *InstancePtr, u32 Data);
u32 XRecord_measure_Get_noise_rms(XRecord_measure *InstancePtr);

void XRecord_measure_InterruptGlobalEnable(XRecord_measure *InstancePtr);
void XRecord_measure_InterruptGlobalDisable(XRecord_measure *InstancePtr);
void XRecord_measure_InterruptEnable(XRecord_measure *InstancePtr, u32 Mask);
void XRecord_measure_InterruptDisable(XRecord_measure *InstancePtr, u32 Mask);
void XRecord_measure_InterruptClear(XRecord_measure *InstancePtr, u32 Mask);
u32 XRecord_measure_InterruptGetEnabled(XRecord_measure *InstancePtr);
u32 XRecord_measure_InterruptGetStatus(XRecord_measure *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
