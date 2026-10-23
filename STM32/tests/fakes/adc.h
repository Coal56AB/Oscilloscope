#ifndef STM32_TEST_ADC_H
#define STM32_TEST_ADC_H
#include "stm32f1xx_hal.h"
typedef struct
{
  unsigned instance;
} ADC_HandleTypeDef;
extern ADC_HandleTypeDef hadc1;
typedef struct
{
  uint32_t Channel, Rank, SamplingTime;
} ADC_ChannelConfTypeDef;
#define ADC_CHANNEL_9 9U
#define ADC_CHANNEL_TEMPSENSOR 16U
#define ADC_CHANNEL_VREFINT 17U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_239CYCLES_5 239U
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *config);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
#endif
