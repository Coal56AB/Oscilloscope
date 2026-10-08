/* Минимальная граница HAL для проверки поведения UART без платы. */
#ifndef STM32_TEST_HAL_H
#define STM32_TEST_HAL_H

#include <stdint.h>

typedef struct
{
  unsigned instance;
} UART_HandleTypeDef;

typedef enum
{
  HAL_OK,
  HAL_ERROR,
  HAL_BUSY,
  HAL_TIMEOUT
} HAL_StatusTypeDef;

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);

#endif
