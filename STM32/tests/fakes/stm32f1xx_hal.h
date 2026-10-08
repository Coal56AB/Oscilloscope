/* Минимальная граница HAL для проверки поведения UART без платы. */
#ifndef STM32_TEST_HAL_H
#define STM32_TEST_HAL_H

#include <stdint.h>
#include <stddef.h>
#define __STATIC_INLINE static inline

typedef struct
{
  uint32_t IDR;
  uint32_t ODR;
} GPIO_TypeDef;

extern GPIO_TypeDef test_gpio_a, test_gpio_b, test_gpio_c, test_gpio_d;
extern uint32_t test_hal_tick;
#define GPIOA (&test_gpio_a)
#define GPIOB (&test_gpio_b)
#define GPIOC (&test_gpio_c)
#define GPIOD (&test_gpio_d)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define GPIO_PIN_0 0x0001U
#define GPIO_PIN_1 0x0002U
#define GPIO_PIN_2 0x0004U
#define GPIO_PIN_3 0x0008U
#define GPIO_PIN_4 0x0010U
#define GPIO_PIN_5 0x0020U
#define GPIO_PIN_6 0x0040U
#define GPIO_PIN_7 0x0080U
#define GPIO_PIN_8 0x0100U
#define GPIO_PIN_9 0x0200U
#define GPIO_PIN_10 0x0400U
#define GPIO_PIN_11 0x0800U
#define GPIO_PIN_12 0x1000U
#define GPIO_PIN_13 0x2000U
#define GPIO_PIN_14 0x4000U
#define GPIO_PIN_15 0x8000U
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

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
