// ==============================================================
// Vivado(TM) HLS - High-Level Synthesis from C, C++ and SystemC v2019.1 (64-bit)
// Copyright 1986-2019 Xilinx, Inc. All Rights Reserved.
// ==============================================================
/***************************** Include Files *********************************/
#include "xrecord_measure.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XRecord_measure_CfgInitialize(XRecord_measure *InstancePtr, XRecord_measure_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XRecord_measure_Start(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL) & 0x80;
    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XRecord_measure_IsDone(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XRecord_measure_IsIdle(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XRecord_measure_IsReady(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XRecord_measure_EnableAutoRestart(XRecord_measure *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XRecord_measure_DisableAutoRestart(XRecord_measure *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_AP_CTRL, 0);
}

void XRecord_measure_Set_codes(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_CODES_DATA, Data);
}

u32 XRecord_measure_Get_codes(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_CODES_DATA);
    return Data;
}

void XRecord_measure_Set_values(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_VALUES_DATA, Data);
}

u32 XRecord_measure_Get_values(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_VALUES_DATA);
    return Data;
}

void XRecord_measure_Set_result(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_RESULT_DATA, Data);
}

u32 XRecord_measure_Get_result(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_RESULT_DATA);
    return Data;
}

void XRecord_measure_Set_count(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_COUNT_DATA, Data);
}

u32 XRecord_measure_Get_count(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_COUNT_DATA);
    return Data;
}

void XRecord_measure_Set_stride(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_STRIDE_DATA, Data);
}

u32 XRecord_measure_Get_stride(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_STRIDE_DATA);
    return Data;
}

void XRecord_measure_Set_filtered(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_FILTERED_DATA, Data);
}

u32 XRecord_measure_Get_filtered(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_FILTERED_DATA);
    return Data;
}

void XRecord_measure_Set_gain(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_GAIN_DATA, Data);
}

u32 XRecord_measure_Get_gain(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_GAIN_DATA);
    return Data;
}

void XRecord_measure_Set_zero(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_ZERO_DATA, Data);
}

u32 XRecord_measure_Get_zero(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_ZERO_DATA);
    return Data;
}

void XRecord_measure_Set_sample_rate(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_SAMPLE_RATE_DATA, Data);
}

u32 XRecord_measure_Get_sample_rate(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_SAMPLE_RATE_DATA);
    return Data;
}

void XRecord_measure_Set_operation(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_OPERATION_DATA, Data);
}

u32 XRecord_measure_Get_operation(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_OPERATION_DATA);
    return Data;
}

void XRecord_measure_Set_filter_level(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_FILTER_LEVEL_DATA, Data);
}

u32 XRecord_measure_Get_filter_level(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_FILTER_LEVEL_DATA);
    return Data;
}

void XRecord_measure_Set_noise_rms(XRecord_measure *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_NOISE_RMS_DATA, Data);
}

u32 XRecord_measure_Get_noise_rms(XRecord_measure *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_NOISE_RMS_DATA);
    return Data;
}

void XRecord_measure_InterruptGlobalEnable(XRecord_measure *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_GIE, 1);
}

void XRecord_measure_InterruptGlobalDisable(XRecord_measure *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_GIE, 0);
}

void XRecord_measure_InterruptEnable(XRecord_measure *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_IER);
    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_IER, Register | Mask);
}

void XRecord_measure_InterruptDisable(XRecord_measure *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_IER);
    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_IER, Register & (~Mask));
}

void XRecord_measure_InterruptClear(XRecord_measure *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XRecord_measure_WriteReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_ISR, Mask);
}

u32 XRecord_measure_InterruptGetEnabled(XRecord_measure *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_IER);
}

u32 XRecord_measure_InterruptGetStatus(XRecord_measure *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XRecord_measure_ReadReg(InstancePtr->Control_BaseAddress, XRECORD_MEASURE_CONTROL_ADDR_ISR);
}

