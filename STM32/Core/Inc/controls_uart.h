/**
 * @file    controls_uart.h
 * @brief   Неблокирующая отправка событий панели через HAL UART.
 */

#ifndef CONTROLS_UART_H
#define CONTROLS_UART_H

#include "controls.h"
#include "stm32f1xx_hal.h"

typedef struct
{
  uint32_t packets;
  uint32_t start_errors;
  uint32_t transfer_errors;
  uint32_t timeouts;
} ControlsUartMetrics;

/** Назначить UART транспортом панели; UART должен быть предварительно настроен CubeMX. */
void controls_uart_init(UART_HandleTypeDef *uart);

/** Обслужить завершение/повтор и начать следующий пакет, не ожидая UART. */
void controls_uart_poll(Stm32Controls *controls, uint32_t now_ms);

/** Передать уведомление HAL о завершении TX; допустимо вызывать из IRQ. */
void controls_uart_tx_complete(UART_HandleTypeDef *uart);

/** Передать уведомление HAL об ошибке UART; восстановление произойдёт в главном цикле. */
void controls_uart_error(UART_HandleTypeDef *uart);

/** Получить счётчики передачи для отладчика; счётчики обновляет главный цикл. */
const ControlsUartMetrics *controls_uart_metrics(void);

#endif /* CONTROLS_UART_H */
