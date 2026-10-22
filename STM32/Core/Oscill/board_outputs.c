/** @file board_outputs.c Реле PA0…PA5 и ADC1_IN9 по актуальной схеме PCB. */
#include "board_outputs.h"
#include "main.h"
#include "adc.h"

static uint8_t measuring;
static uint32_t last_measure_ms;
static uint32_t started_ms;

/**
 * @brief   Откалибровать ADC после настройки CubeMX до начала опроса панели.
 * @details Калибровка выполняется однократно при старте. Последующие замеры
 *          не ждут завершения ADC и не задерживают чтение энкодеров.
 */
void board_outputs_init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  measuring = 0U;
  last_measure_ms = HAL_GetTick() - 1000U;
}

/**
 * @brief   Одним BSRR применить маску A1/A2/A3/B1/B2/B3, активный HIGH.
 * @details Все линии принадлежат GPIOA, порядок битов протокола не совпадает
 *          с порядком PA: A1=PA3, A2=PA4, A3=PA5, B1=PA2, B2=PA1, B3=PA0.
 */
void board_outputs_relays(uint8_t mask)
{
  uint32_t pins = ((mask & 7U) << 3) | ((mask & 8U) >> 1) |
                  ((mask & 16U) >> 3) | ((mask & 32U) >> 5);
  GPIOA->BSRR = pins | ((63U & ~pins) << 16);
}

/**
 * @brief   Раз в секунду запустить замер и вернуть готовый сырой отсчёт.
 * @details Poll с нулевым таймаутом не ждёт ADC. При сбое замер отменяется,
 *          следующая попытка выполняется по обычному периоду без ложных данных.
 */
int board_outputs_power(uint32_t now_ms, unsigned *raw)
{
  if (measuring)
  {
    if (HAL_ADC_PollForConversion(&hadc1, 0U) == HAL_OK)
    {
      *raw = (unsigned)HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);
      measuring = 0U;
      return 1;
    }
    if ((uint32_t)(now_ms - started_ms) >= 10U)
    {
      HAL_ADC_Stop(&hadc1);
      measuring = 0U;
    }
  }
  else if ((uint32_t)(now_ms - last_measure_ms) >= 1000U)
  {
    last_measure_ms = now_ms;
    if (HAL_ADC_Start(&hadc1) == HAL_OK)
    {
      measuring = 1U;
      started_ms = now_ms;
    }
  }
  return 0;
}
