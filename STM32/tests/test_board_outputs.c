/** @file test_board_outputs.c Проверки фактической карты реле и обслуживания ADC. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "board_outputs.h"
#include "main.h"
#include "adc.h"
#include <assert.h>

GPIO_TypeDef test_gpioa;
ADC_HandleTypeDef hadc1;
static unsigned starts, stops;
static HAL_StatusTypeDef start_status = HAL_OK, conversion_status = HAL_TIMEOUT;

/** Часы старта нужны для немедленного первого измерения после калибровки. */
uint32_t HAL_GetTick(void)
{
  return 0;
}
/** Любая ошибка настройки должна завершать тест конкретным сбоем. */
void Error_Handler(void)
{
  assert(0);
}
/** Калибровка выполняется на том же ADC, что используется при измерении. */
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1);
  return HAL_OK;
}
/** Сохранить число реальных запусков, чтобы обнаружить блокирующий цикл. */
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1);
  ++starts;
  return start_status;
}
/** Завершение и таймаут обязаны освобождать ADC. */
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1);
  ++stops;
  return HAL_OK;
}
/** Нулевое ожидание является существенным условием обслуживания панели. */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{
  assert(adc == &hadc1 && timeout == 0);
  return conversion_status;
}
/** Отсчёт доступен только после успешного завершения. */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && conversion_status == HAL_OK);
  return 2048;
}
/** Проверить каждую линию реле и восстановление ADC после пропущенного замера. */
int main(void)
{
  static const unsigned pins[6] = {3, 4, 5, 2, 1, 0};
  unsigned i, raw = 123;
  for (i = 0; i < 6; ++i)
  {
    uint32_t set = 1U << pins[i];
    board_outputs_relays((uint8_t)(1U << i));
    assert(test_gpioa.BSRR == (set | ((63U & ~set) << 16)));
  }
  board_outputs_init();
  assert(!board_outputs_power(0, &raw) && starts == 1 && raw == 123);
  assert(!board_outputs_power(1, &raw) && starts == 1 && stops == 0);
  conversion_status = HAL_OK;
  assert(board_outputs_power(2, &raw) && raw == 2048 && stops == 1);
  assert(!board_outputs_power(999, &raw) && starts == 1);
  conversion_status = HAL_TIMEOUT;
  assert(!board_outputs_power(1000, &raw) && starts == 2);
  assert(!board_outputs_power(1010, &raw) && stops == 2);
  start_status = HAL_ERROR;
  assert(!board_outputs_power(2000, &raw) && starts == 3);
  assert(!board_outputs_power(2001, &raw) && starts == 3);
  start_status = HAL_OK;
  assert(!board_outputs_power(3000, &raw) && starts == 4);
  conversion_status = HAL_OK;
  assert(board_outputs_power(3001, &raw) && stops == 3);
  return 0;
}
