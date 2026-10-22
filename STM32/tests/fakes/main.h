#ifndef STM32_TEST_MAIN_H
#define STM32_TEST_MAIN_H
#include "stm32f1xx_hal.h"
typedef struct
{
  uint32_t BSRR;
} GPIO_TypeDef;
extern GPIO_TypeDef test_gpioa;
#define GPIOA (&test_gpioa)
uint32_t HAL_GetTick(void);
void Error_Handler(void);
#endif
