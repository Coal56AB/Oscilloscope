/** @file test_board_outputs.c Реле и неблокирующий цикл питания/температуры ADC. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "board_outputs.h"
#include "main.h"
#include "adc.h"
#include <assert.h>

GPIO_TypeDef test_gpioa;
ADC_HandleTypeDef hadc1;
static unsigned starts, stops, selected, configs;
static unsigned values[3] = {2048, 2145, 1800};
static uint32_t clock_ms;
static int active, fail_config = -1, fail_start = -1, fail_poll = -1, hanging = -1;

/** Имитировать миллисекундные часы, включая переполнение uptime. */
uint32_t HAL_GetTick(void) { return clock_ms; }
/** Ошибка настройки должна завершать тест конкретным сбоем. */
void Error_Handler(void) { assert(0); }
/** Калибровать единственный ADC только до обслуживания цикла. */
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && !active);
  return HAL_OK;
}
/** Не разрешать смену канала, пока предыдущий замер ещё работает. */
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *config)
{
  assert(adc == &hadc1 && !active);
  assert(config->Rank == ADC_REGULAR_RANK_1 && config->SamplingTime == ADC_SAMPLETIME_239CYCLES_5);
  selected = config->Channel == ADC_CHANNEL_9 ? BOARD_ADC_POWER :
             config->Channel == ADC_CHANNEL_TEMPSENSOR ? BOARD_ADC_TEMPERATURE : BOARD_ADC_REFERENCE;
  assert(config->Channel == ADC_CHANNEL_9 || config->Channel == ADC_CHANNEL_TEMPSENSOR ||
         config->Channel == ADC_CHANNEL_VREFINT);
  ++configs;
  return (int)selected == fail_config ? HAL_ERROR : HAL_OK;
}
/** Зафиксировать настоящий запуск, исключая неявное блокирующее ожидание. */
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && !active);
  ++starts;
  if ((int)selected == fail_start) return HAL_ERROR;
  active = 1;
  return HAL_OK;
}
/** Завершение и таймаут освобождают ADC перед следующим каналом. */
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && active);
  active = 0;
  ++stops;
  return HAL_OK;
}
/** Любой ненулевой timeout означал бы блокировку опроса энкодеров. */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{
  assert(adc == &hadc1 && active && timeout == 0);
  return (int)selected == fail_poll ? HAL_ERROR : (int)selected == hanging ? HAL_TIMEOUT : HAL_OK;
}
/** Возвращать отсчёт выбранного канала, чтобы поймать смешение ADC9/16/17. */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && active);
  return values[selected];
}
/** Новый сценарий не должен наследовать данные или сбой предыдущего цикла. */
static void reset(void)
{
  assert(!active);
  starts = stops = configs = 0;
  fail_config = fail_start = fail_poll = hanging = -1;
  clock_ms = 0;
  board_outputs_init();
}
/** Проверить GPIO и готовность/частичный отказ трёх независимых измерений. */
int main(void)
{
  static const unsigned pins[6] = {3, 4, 5, 2, 1, 0};
  BoardAdcSample sample = {{123, 123, 123}, 99};
  unsigned i;
  for (i = 0; i < 6; ++i)
  {
    uint32_t set = 1U << pins[i];
    board_outputs_relays((uint8_t)(1U << i));
    assert(test_gpioa.BSRR == (set | ((63U & ~set) << 16)));
  }
  reset();
  assert(!board_outputs_sample(0, &sample) && starts == 1 && sample.valid == 99);
  assert(!board_outputs_sample(1, &sample) && selected == BOARD_ADC_TEMPERATURE);
  assert(!board_outputs_sample(2, &sample) && selected == BOARD_ADC_REFERENCE);
  assert(board_outputs_sample(3, &sample) && sample.valid == 7 && starts == 3 && stops == 3);
  assert(sample.raw[0] == 2048 && sample.raw[1] == 2145 && sample.raw[2] == 1800);
  assert(!board_outputs_sample(999, &sample) && starts == 3);
  assert(!board_outputs_sample(1000, &sample));
  assert(!board_outputs_sample(1001, &sample));
  assert(!board_outputs_sample(1002, &sample));
  assert(board_outputs_sample(1003, &sample) && sample.valid == 7);

  reset();hanging = BOARD_ADC_POWER;
  clock_ms = UINT32_MAX - 5U;board_outputs_init();
  assert(!board_outputs_sample(clock_ms, &sample));
  assert(!board_outputs_sample(3, &sample) && stops == 0);
  assert(!board_outputs_sample(4, &sample) && stops == 1);
  assert(!board_outputs_sample(5, &sample));
  assert(board_outputs_sample(6, &sample) && sample.valid == 6 && stops == 3);

  reset();fail_start = BOARD_ADC_POWER;
  assert(!board_outputs_sample(0, &sample));
  assert(!board_outputs_sample(1, &sample));
  assert(!board_outputs_sample(2, &sample));
  assert(board_outputs_sample(3, &sample) && sample.valid == 6);
  reset();fail_config = BOARD_ADC_TEMPERATURE;
  assert(!board_outputs_sample(0, &sample));
  assert(!board_outputs_sample(1, &sample));
  assert(!board_outputs_sample(2, &sample));
  assert(board_outputs_sample(3, &sample) && sample.valid == 5 && starts == 2);
  reset();fail_poll = BOARD_ADC_REFERENCE;
  assert(!board_outputs_sample(0, &sample));
  assert(!board_outputs_sample(1, &sample));
  assert(!board_outputs_sample(2, &sample));
  assert(board_outputs_sample(3, &sample) && sample.valid == 3);
  reset();values[BOARD_ADC_REFERENCE] = 4096;
  assert(!board_outputs_sample(0, &sample));
  assert(!board_outputs_sample(1, &sample));
  assert(!board_outputs_sample(2, &sample));
  assert(board_outputs_sample(3, &sample) && sample.valid == 3);
  return 0;
}
