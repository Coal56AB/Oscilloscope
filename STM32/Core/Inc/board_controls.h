/**
 * @file    board_controls.h
 * @brief   Чтение панели по соединениям Interfaces.SchDoc.
 */

#ifndef BOARD_CONTROLS_H
#define BOARD_CONTROLS_H

#include "controls.h"

/** Получить логические состояния панели; активный низкий уровень кнопок нормализуется. */
void board_controls_read(uint8_t buttons[CONTROL_ID_COUNT], uint8_t encoders[STM32_ENCODER_COUNT]);

#endif /* BOARD_CONTROLS_H */
