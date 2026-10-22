/** @file board_outputs.h Аппаратные выходы и асинхронное измерение питания. */
#ifndef BOARD_OUTPUTS_H
#define BOARD_OUTPUTS_H
#include <stdint.h>
/** Калибровка после MX_ADC1_Init, до начала циклического опроса панели. */
void board_outputs_init(void);
/** Одновременно применить шесть битов A1/A2/A3/B1/B2/B3 к GPIO. */
void board_outputs_relays(uint8_t mask);
/** Обслужить одиночный ADC замер; вернуть 1 и raw только при готовом результате. */
int board_outputs_power(uint32_t now_ms, unsigned *raw);
#endif
