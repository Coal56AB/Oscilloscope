/** @file board_outputs.h Аппаратные выходы и асинхронное измерение питания. */
#ifndef BOARD_OUTPUTS_H
#define BOARD_OUTPUTS_H
#include <stdint.h>
/** Калибровка после MX_ADC1_Init, до начала циклического опроса панели. */
void board_outputs_init(void);
/** Одновременно применить шесть битов A1/A2/A3/B1/B2/B3 к GPIO. */
void board_outputs_relays(uint8_t mask);
enum { BOARD_ADC_POWER, BOARD_ADC_TEMPERATURE, BOARD_ADC_REFERENCE, BOARD_ADC_CHANNELS };
typedef struct
{
  unsigned raw[BOARD_ADC_CHANNELS];
  uint8_t valid;
} BoardAdcSample;
/** Обслужить неблокирующий цикл ADC9/16/17; valid отмечает только готовые каналы. */
int board_outputs_sample(uint32_t now_ms, BoardAdcSample *sample);
#endif
