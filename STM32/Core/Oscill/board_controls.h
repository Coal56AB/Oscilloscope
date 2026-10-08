/**
 * @file    board_controls.h
 * @brief   Чтение панели по соединениям Interfaces.SchDoc.
 */

#ifndef BOARD_CONTROLS_H
#define BOARD_CONTROLS_H

#include "controls.h"

/** Привязать библиотеки к настроенным GPIO и принять исходное состояние панели. */
void board_controls_init(void);
/** Опросить общие обработчики входов и передать события приложению. */
void board_controls_poll(Stm32Controls *controls, uint32_t now_ms);
/** Выставить индикатор LED_RUN на PC7; активный уровень 1. */
void board_controls_set_run_led(uint8_t enabled);

#endif /* BOARD_CONTROLS_H */
