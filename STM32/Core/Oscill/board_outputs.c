/** @file board_outputs.c Реле PA0…PA5 и неблокирующий цикл ADC1_IN9/16/17. */
#include "board_outputs.h"
#include "main.h"
#include "adc.h"

static uint8_t measuring;
static unsigned channel;
static BoardAdcSample pending;
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
  channel = BOARD_ADC_CHANNELS;
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
 * @brief   Раз в секунду получить питание, датчик кристалла и внутреннюю опору.
 * @details ADC9/16/17 используют один ADC последовательно, без ожидания готовности.
 *          239,5 такта при настроенных 10,67 МГц дают 22,45 мкс выборки,
 *          больше требуемых DS5792 17,1 мкс. HAL включает путь TSVREFE и
 *          выдерживает его стартовые 10 мкс однократно при выборе ADC16.
 *          Сбой одного канала не отменяет другие; наружу уходят raw и valid.
 */
int board_outputs_sample(uint32_t now_ms, BoardAdcSample *sample)
{
  static const uint32_t channels[BOARD_ADC_CHANNELS] = {
      ADC_CHANNEL_9, ADC_CHANNEL_TEMPSENSOR, ADC_CHANNEL_VREFINT};
  if (channel == BOARD_ADC_CHANNELS)
  {
    if ((uint32_t)(now_ms - last_measure_ms) < 1000U)
    {
      return 0;
    }
    last_measure_ms = now_ms;
    pending.valid = 0U;
    channel = 0U;
  }
  if (measuring)
  {
    HAL_StatusTypeDef result = HAL_ADC_PollForConversion(&hadc1, 0U);
    if (result == HAL_OK)
    {
      pending.raw[channel] = (unsigned)HAL_ADC_GetValue(&hadc1);
      if (pending.raw[channel] <= 4095U)
      {
        pending.valid |= (uint8_t)(1U << channel);
      }
      HAL_ADC_Stop(&hadc1);
      measuring = 0U;
      ++channel;
    }
    else if (result == HAL_ERROR || (uint32_t)(now_ms - started_ms) >= 10U)
    {
      HAL_ADC_Stop(&hadc1);
      measuring = 0U;
      ++channel;
    }
    else
    {
      return 0;
    }
  }
  if (channel < BOARD_ADC_CHANNELS)
  {
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channels[channel];
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &config) == HAL_OK && HAL_ADC_Start(&hadc1) == HAL_OK)
    {
      measuring = 1U;
      started_ms = now_ms;
    }
    else
    {
      ++channel;
    }
  }
  if (channel == BOARD_ADC_CHANNELS)
  {
    *sample = pending;
    return 1;
  }
  return 0;
}
