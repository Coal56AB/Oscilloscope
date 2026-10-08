/** Управляемые уровни контактов для проверки настоящего адаптера платы. */
#include "stm32f1xx_hal.h"
#include <stdlib.h>

GPIO_TypeDef test_gpio_a, test_gpio_b, test_gpio_c, test_gpio_d;
uint32_t test_hal_tick;

/** Вернуть часы сценария без ожидания настоящего SysTick. */
uint32_t HAL_GetTick(void)
{
  return test_hal_tick;
}

/** Прочитать заданное тестом состояние физического контакта. */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  return (port->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/** Сохранить выход для проверки полярности и независимости LED. */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  if (state == GPIO_PIN_SET)
  {
    port->ODR |= pin;
  }
  else
  {
    port->ODR &= ~(uint32_t)pin;
  }
}

/** Смоделировать переключение выхода библиотекой. */
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
  port->ODR ^= pin;
}

/** Ошибка инициализации должна завершить тест, как остановку приложения. */
void Error_Handler(void)
{
  abort();
}
