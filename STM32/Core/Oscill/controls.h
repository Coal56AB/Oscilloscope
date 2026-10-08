/**
 * @file    controls.h
 * @brief   События панели и очередь отправки без зависимости от HAL.
 */

#ifndef STM32_CONTROLS_H
#define STM32_CONTROLS_H
#include "control_protocol.h"

#define STM32_ENCODER_COUNT 5
#define STM32_TX_EVENTS 64

typedef struct
{
  ControlEvent events[STM32_TX_EVENTS];
  unsigned head;
  unsigned count;
  uint16_t sequence;
  uint32_t last_heartbeat_ms;
  uint64_t dropped;
  uint64_t invalid_transitions;
} Stm32Controls;

/** Подготовить очередь панели; вернуть 0 при неверном указателе. */
int stm32_controls_init(Stm32Controls *controls);
/** Принять готовое DOWN/UP или шаг энкодера после обработки библиотекой. */
void stm32_controls_input(Stm32Controls *controls, ControlEventType type, unsigned id,
                          int value, uint32_t timestamp_ms);
/** Обслужить heartbeat по миллисекундному счётчику. */
void stm32_controls_poll(Stm32Controls *controls, uint32_t now_ms);
/** Подготовить пакет, сохранив событие в очереди до подтверждения транспорта. */
int stm32_controls_peek(const Stm32Controls *controls, uint8_t packet[CONTROL_PACKET_SIZE]);
/** Освободить старейшее событие после отправки; вызывает только главный цикл. */
void stm32_controls_sent(Stm32Controls *controls);
#endif
