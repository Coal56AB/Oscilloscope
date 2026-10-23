/** Минимальная граница GPIO для испытания настоящего декодера фаз без платы. */
#ifndef STM32_ENCODER_TEST_CONFIG_H
#define STM32_ENCODER_TEST_CONFIG_H
#include "../stm32f1xx_hal.h"

typedef struct
{
  uint32_t IDR;
} GPIO_TypeDef;

typedef enum
{
  GPIO_PIN_RESET,
  GPIO_PIN_SET
} GPIO_PinState;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
#endif
