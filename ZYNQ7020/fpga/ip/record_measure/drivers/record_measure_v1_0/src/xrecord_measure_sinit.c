// ==============================================================
// Vivado(TM) HLS - High-Level Synthesis from C, C++ and SystemC v2019.1 (64-bit)
// Copyright 1986-2019 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xrecord_measure.h"

extern XRecord_measure_Config XRecord_measure_ConfigTable[];

XRecord_measure_Config *XRecord_measure_LookupConfig(u16 DeviceId) {
	XRecord_measure_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XRECORD_MEASURE_NUM_INSTANCES; Index++) {
		if (XRecord_measure_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XRecord_measure_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XRecord_measure_Initialize(XRecord_measure *InstancePtr, u16 DeviceId) {
	XRecord_measure_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XRecord_measure_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XRecord_measure_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

