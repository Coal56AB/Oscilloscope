/**
 * @file    controls.h
 * @brief   Переносимая обработка органов управления осциллографа.
 * @details Драйвер платы передаёт нормализованные состояния GPIO. Обработчик
 *          не зависит от HAL, распиновки, UART и графического интерфейса.
 */

#ifndef STM32_CONTROLS_H
#define STM32_CONTROLS_H
#include "control_protocol.h"

#define STM32_ENCODER_COUNT 5
#define STM32_TX_EVENTS 64

typedef struct
{
  uint8_t stable;
  uint8_t candidate;
  uint32_t changed_ms;
} ControlButton;

typedef struct
{
  ControlButton buttons[CONTROL_ID_COUNT];
  uint8_t encoder_state[STM32_ENCODER_COUNT];
  int8_t encoder_steps[STM32_ENCODER_COUNT];
  unsigned steps_per_detent;
  unsigned debounce_ms;
  ControlEvent events[STM32_TX_EVENTS];
  unsigned head;
  unsigned count;
  uint16_t sequence;
  uint32_t last_heartbeat_ms;
  uint64_t dropped;
  uint64_t invalid_transitions;
} Stm32Controls;

/** Принять исходные уровни, не создавая ложных событий при включении питания. */
int stm32_controls_init(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                        const uint8_t encoders[STM32_ENCODER_COUNT], unsigned steps_per_detent,
                        unsigned debounce_ms);
/** Превратить очередной снимок панели в шаги энкодеров, DOWN/UP и heartbeat. */
void stm32_controls_scan(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                         const uint8_t encoders[STM32_ENCODER_COUNT], uint32_t now_ms);
/** Подготовить пакет, сохранив событие в очереди до подтверждения транспорта. */
int stm32_controls_peek(const Stm32Controls *controls, uint8_t packet[CONTROL_PACKET_SIZE]);
/** Освободить старейшее событие после отправки; вызывает только главный цикл. */
void stm32_controls_sent(Stm32Controls *controls);
#endif
