/** @file panel_state.h Состояние масштабов и выходов, без аппаратных зависимостей. */
#ifndef PANEL_STATE_H
#define PANEL_STATE_H
#include "controls.h"

typedef struct
{
  Stm32Controls *events;
  int scale[2];
  int applied[2];
  uint8_t position_mode[2];
  uint8_t run;
  uint8_t relay_mask;
  uint8_t switching;
  uint8_t host_connected;
  uint32_t host_ms;
  uint32_t switched_ms;
  uint32_t reported_ms;
} PanelState;

/** Назначить очередь и исходные масштабы; GPIO применит первый poll. */
void panel_state_init(PanelState *state, Stm32Controls *events);
/** Принять готовый шаг/нажатие после аппаратного декодера и фильтра. */
void panel_state_input(PanelState *state, ControlEventType type, unsigned id, int value,
                       uint32_t now_ms);
/** Применить проверенный запрос GUI; не вызывать из IRQ. */
void panel_state_command(PanelState *state, const ControlEvent *command, uint32_t now_ms);
/** Обновить GPIO callback и опубликовать масштаб после времени установления. */
void panel_state_poll(PanelState *state, uint32_t now_ms, void (*write_relays)(uint8_t));
/** Вернуть трёхбитную маску ступени одного канала по индексу масштаба. */
uint8_t panel_state_relays(int scale_index);
/** Пересчитать сырой ADC9 с учётом опоры и делителя питания платы. */
unsigned panel_state_power_mv(unsigned adc_value);
/** Оценить температуру кристалла по ADC16/17; неверные отсчёты не дают показания. */
int panel_state_temperature_mc(unsigned sensor_raw, unsigned reference_raw, int *millicelsius);
#endif
