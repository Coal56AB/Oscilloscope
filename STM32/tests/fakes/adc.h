#ifndef STM32_TEST_ADC_H
#define STM32_TEST_ADC_H
#include "stm32f1xx_hal.h"
typedef struct
{
  unsigned instance;
} ADC_HandleTypeDef;
extern ADC_HandleTypeDef hadc1;
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
#endif
