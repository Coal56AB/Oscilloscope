/** @file panel_state.c Масштаб принадлежит STM32; GUI получает итог после переключения. */
#include "panel_state.h"
#include "oscill_config.h"
#include <string.h>

/** Автономная отладка действует только при отсутствии команд GUI две секунды. */
static int standalone(const PanelState *state, uint32_t now_ms)
{
  return STM32_STANDALONE_DEBUG &&
         (!state->host_connected || (uint32_t)(now_ms - state->host_ms) >= 2000U);
}

/**
 * @brief   Выбрать аппаратную ступень для поддерживаемого масштаба входа.
 * @details DiffMeasure.SchDoc: HIGH открывает NC-шунт резистора.
 *          111: 962210 Ом; 110: 52210 Ом; 100: 5210 Ом к GND,
 *          входная цепочка 1 МОм, AD8138 имеет единичный дифференциальный gain.
 *          Таблица рассчитана для 8 делений и номинальных 1,024 Vpp AD9288.
 */
uint8_t panel_state_relays(int scale_index)
{
  static const uint8_t masks[CONTROL_SCALE_COUNT] = {7U, 7U, 6U, 6U, 6U, 4U};
  return scale_index >= 0 && scale_index < CONTROL_SCALE_COUNT ? masks[scale_index] : 0U;
}

/**
 * @brief   Начать с масштабов, соответствующих исходному состоянию GUI.
 * @details Первое обслуживание обязательно применяет GPIO и выдерживает паузу;
 *          до этого никакой масштаб не объявляется установленным.
 */
void panel_state_init(PanelState *state, Stm32Controls *events)
{
  memset(state, 0, sizeof(*state));
  state->events = events;
  state->scale[0] = 2;
  state->scale[1] = 3;
  state->applied[0] = state->applied[1] = -1;
  state->relay_mask = 255U;
}

/**
 * @brief   Применить шаги каналов на STM32, остальные события передать GUI.
 * @details Положительный шаг уменьшает В/дел. В режиме позиции дельта остаётся
 *          событием GUI. При автономной отладке энкодеры всегда выбирают
 *          масштаб, RUN переключает LED.
 */
void panel_state_input(PanelState *state, ControlEventType type, unsigned id, int value,
                       uint32_t now_ms)
{
  if (type == CONTROL_ROTATE && id < 2 && (value == -1 || value == 1) &&
      (standalone(state, now_ms) || !state->position_mode[id]))
  {
    int next = state->scale[id] - value;
    if (next >= 0 && next < CONTROL_SCALE_COUNT)
    {
      state->scale[id] = next;
    }
    return;
  }
  if (standalone(state, now_ms) && type == CONTROL_DOWN && id == 10U)
  {
    state->run = !state->run;
  }
  stm32_controls_input(state->events, type, id, value, now_ms);
}

/**
 * @brief   Принять запрос GUI, сохранив единственного владельца масштабов.
 * @details Абсолютный масштаб проверяется общей таблицей. Повтор запроса
 *          безвреден; пакеты событий и телеметрия не являются командами.
 */
void panel_state_command(PanelState *state, const ControlEvent *command, uint32_t now_ms)
{
  if (command->type == CONTROL_SET_SCALE && command->control < 2)
  {
    int index = control_scale_index(command->value);
    if (index >= 0)
    {
      state->scale[command->control] = index;
    }
    else
    {
      return;
    }
  }
  else if (command->type == CONTROL_SET_RUN &&
           command->control == 255U && (command->value == 0 || command->value == 1))
  {
    state->run = (uint8_t)command->value;
  }
  else if (command->type == CONTROL_SET_ENCODER_MODE &&
           command->control < 2 && (command->value == 0 || command->value == 1))
  {
    state->position_mode[command->control] = (uint8_t)command->value;
  }
  else
  {
    return;
  }
  state->host_connected = 1U;
  state->host_ms = now_ms;
}

/**
 * @brief   Применить реле и опубликовать итог после времени установления.
 * @details Callback меняет только GPIO. Новый поворот во время переключения
 *          заменяет цель; пауза начинается заново лишь при изменении маски.
 *          Периодический снимок восстанавливает GUI после потерь или перезапуска.
 */
void panel_state_poll(PanelState *state, uint32_t now_ms, void (*write_relays)(uint8_t))
{
  unsigned i;
  if (state->host_connected && (uint32_t)(now_ms - state->host_ms) >= 2000U)
  {
    state->host_connected = 0U;
  }
  uint8_t mask = panel_state_relays(state->scale[0]) |
                 (uint8_t)(panel_state_relays(state->scale[1]) << 3);
  if (mask != state->relay_mask)
  {
    write_relays(mask);
    state->relay_mask = mask;
    state->switching = 1U;
    state->switched_ms = now_ms;
  }
  if (state->switching && (uint32_t)(now_ms - state->switched_ms) < STM32_RELAY_SETTLE_MS)
  {
    return;
  }
  state->switching = 0U;
  for (i = 0; i < 2; ++i)
  {
    if (state->applied[i] != state->scale[i] || (uint32_t)(now_ms - state->reported_ms) >= 1000U)
    {
      state->applied[i] = state->scale[i];
      stm32_controls_report(state->events, CONTROL_SCALE, i,
                             control_scale_mv((unsigned)state->applied[i]), now_ms);
    }
  }
  if ((uint32_t)(now_ms - state->reported_ms) >= 1000U)
  {
    state->reported_ms = now_ms;
  }
}

/**
 * @brief   Пересчитать ADC9 в напряжение до делителя R32/R33 100k/100k.
 * @details Используется заданная опора VREF+, а не измеренное значение опоры.
 *          Округление целочисленное; результат не является процентом заряда.
 */
unsigned panel_state_power_mv(unsigned adc_value)
{
  if (adc_value > 4095U)
  {
    adc_value = 4095U;
  }
  return (adc_value * STM32_ADC_REFERENCE_MV * 2U + 2047U) / 4095U;
}

/**
 * @brief   Пересчитать ADC16 в температуру кристалла с учётом ADC17/VREFINT.
 * @details DS5792: V25=1,43 В, slope=4,3 мВ/°C, VREFINT=1,20 В типовые.
 *          Отношение отсчётов компенсирует VDDA, но не заменяет калибровку
 *          датчика и внутренней опоры. Отказ не перезаписывает результат.
 */
int panel_state_temperature_mc(unsigned sensor_raw, unsigned reference_raw, int *millicelsius)
{
  uint64_t vdda_mv, sensor_uv;
  int64_t delta, result;
  if (!millicelsius || !sensor_raw || sensor_raw >= 4095U ||
      !reference_raw || reference_raw >= 4095U)
  {
    return 0;
  }
  vdda_mv = (4095ULL * STM32_VREFINT_MV + reference_raw / 2U) / reference_raw;
  if (vdda_mv < 2400U || vdda_mv > 3600U)
  {
    return 0;
  }
  sensor_uv = ((uint64_t)sensor_raw * STM32_VREFINT_MV * 1000U + reference_raw / 2U) / reference_raw;
  delta = ((int64_t)STM32_TEMPERATURE_V25_MV * 1000 - (int64_t)sensor_uv) * 1000;
  delta += delta >= 0 ? STM32_TEMPERATURE_SLOPE_UV_PER_C / 2 :
                       -(int64_t)(STM32_TEMPERATURE_SLOPE_UV_PER_C / 2);
  result = 25000 + delta / STM32_TEMPERATURE_SLOPE_UV_PER_C;
  if (result < -40000 || result > 125000)
  {
    return 0;
  }
  *millicelsius = (int)result;
  return 1;
}
