/**
 * @file    controls.c
 * @brief   События панели: энкодеры, кнопки и контроль присутствия по UART.
 */

#include "controls.h"
#include <string.h>

/**
 * @brief   Сохранить изменение панели до освобождения UART.
 * @details Номер назначается и потерянному событию: получатель обнаружит
 *          пропуск и отменит незавершённые удержания. При полной очереди
 *          старые события сохраняются, новое учитывается в dropped.
 * @note    Очередью владеет главный цикл; обработчики IRQ её не изменяют.
 */
static void enqueue(Stm32Controls *controls, ControlEventType type, unsigned id, int value,
                    uint32_t now)
{
  ControlEvent event = {type, (uint8_t)id, controls->sequence++, now, value};
  if (controls->count == STM32_TX_EVENTS)
  {
    ++controls->dropped;
    return;
  }
  controls->events[(controls->head + controls->count) % STM32_TX_EVENTS] = event;
  ++controls->count;
}

/**
 * @brief   Подготовить обработчик к опросу фактической панели.
 * @details Начальные уровни принимаются за установившиеся: включение питания
 *          не создаёт ложных вращений и нажатий. Порог детента согласуется
 *          с механическим энкодером, debounce — с дребезгом кнопок.
 * @param   buttons  1 означает нажатую кнопку, 0 — отпущенную.
 * @param   encoders Двухбитные состояния фаз A/B, по одному на энкодер.
 * @return  1 при допустимых параметрах; 0, если начать опрос невозможно.
 */
int stm32_controls_init(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                        const uint8_t encoders[STM32_ENCODER_COUNT], unsigned steps,
                        unsigned debounce)
{
  unsigned i;
  if (!controls || !buttons || !encoders || !steps || steps > 4 || debounce > 1000)
  {
    return 0;
  }
  memset(controls, 0, sizeof(*controls));
  controls->steps_per_detent = steps;
  controls->debounce_ms = debounce;
  for (i = 0; i < CONTROL_ID_COUNT; ++i)
  {
    controls->buttons[i].stable = controls->buttons[i].candidate = !!buttons[i];
  }
  for (i = 0; i < STM32_ENCODER_COUNT; ++i)
  {
    controls->encoder_state[i] = encoders[i] & 3;
  }
  return 1;
}

/**
 * @brief   Превратить снимок входов в события для GUI.
 * @details Полные детенты дают ROTATE; устойчивые изменения кнопок — DOWN/UP
 *          с временем начала перехода. Длительность удержания определяет GUI.
 *          Heartbeat позволяет отличить неподвижную панель от потери связи.
 * @note    Вызывать регулярно из главного цикла. now — счётчик миллисекунд;
 *          переполнение uint32_t учитывается при сравнении времени.
 */
void stm32_controls_scan(Stm32Controls *controls, const uint8_t buttons[CONTROL_ID_COUNT],
                         const uint8_t encoders[STM32_ENCODER_COUNT], uint32_t now)
{
  static const int8_t direction[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
  unsigned i;
  for (i = 0; i < STM32_ENCODER_COUNT; ++i)
  {
    uint8_t previous = controls->encoder_state[i], next = encoders[i] & 3;
    if ((previous ^ next) == 3)
    {
      /* Обе фазы изменились сразу: направление потеряно.
         Не превращать незавершённый детент в ложный шаг
         после пропуска опроса. */
      ++controls->invalid_transitions;
      controls->encoder_steps[i] = 0;
    }
    else
    {
      controls->encoder_steps[i] += direction[previous * 4 + next];
    }
    controls->encoder_state[i] = next;
    if (controls->encoder_steps[i] >= (int)controls->steps_per_detent)
    {
      enqueue(controls, CONTROL_ROTATE, i, 1, now);
      controls->encoder_steps[i] -= (int8_t)controls->steps_per_detent;
    }
    if (controls->encoder_steps[i] <= -(int)controls->steps_per_detent)
    {
      enqueue(controls, CONTROL_ROTATE, i, -1, now);
      controls->encoder_steps[i] += (int8_t)controls->steps_per_detent;
    }
  }
  for (i = 0; i < CONTROL_ID_COUNT; ++i)
  {
    ControlButton *button = &controls->buttons[i];
    uint8_t value = !!buttons[i];
    if (value != button->candidate)
    {
      button->candidate = value;
      button->changed_ms = now;
    }
    if (button->stable != button->candidate &&
        (uint32_t)(now - button->changed_ms) >= controls->debounce_ms)
    {
      button->stable = button->candidate;
      enqueue(controls, button->stable ? CONTROL_DOWN : CONTROL_UP, i, 0, button->changed_ms);
    }
  }
  if ((uint32_t)(now - controls->last_heartbeat_ms) >= 1000)
  {
    enqueue(controls, CONTROL_HEARTBEAT, 255, 0, now);
    controls->last_heartbeat_ms = now;
  }
}

/**
 * @brief   Сформировать старейший пакет без удаления события из очереди.
 * @details Повторная попытка после занятости или ошибки UART сохраняет данные
 *          и sequence. Транспорт отдельно подтверждает успешную отправку.
 * @return  1 при готовом пакете, 0 при пустой очереди.
 */
int stm32_controls_peek(const Stm32Controls *controls, uint8_t packet[CONTROL_PACKET_SIZE])
{
  return controls->count ? control_encode(packet, &controls->events[controls->head]) : 0;
}

/**
 * @brief   Освободить событие после подтверждения транспортом.
 * @details Продвигает очередь только из главного цикла. IRQ завершения UART
 *          сообщает о результате, но не меняет состояние панели.
 */
void stm32_controls_sent(Stm32Controls *controls)
{
  if (controls->count)
  {
    controls->head = (controls->head + 1) % STM32_TX_EVENTS;
    --controls->count;
  }
}
