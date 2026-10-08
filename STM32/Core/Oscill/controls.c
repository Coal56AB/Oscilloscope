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
 * @brief   Подготовить очередь событий панели без аппаратных зависимостей.
 * @details Дребезг и фазы энкодеров обрабатываются общей библиотекой до этого API.
 */
int stm32_controls_init(Stm32Controls *controls)
{
  if (!controls)
  {
    return 0;
  }
  memset(controls, 0, sizeof(*controls));
  return 1;
}

/**
 * @brief   Сохранить готовое событие кнопки или энкодера.
 * @details Принимает время фактического изменения входа, до задержки фильтра.
 *          Неверные идентификаторы и типы не должны попадать в общий протокол.
 */
void stm32_controls_input(Stm32Controls *controls, ControlEventType type, unsigned id,
                          int value, uint32_t timestamp_ms)
{
  if (!controls)
  {
    return;
  }
  if (((type == CONTROL_DOWN || type == CONTROL_UP) && id < CONTROL_ID_COUNT) ||
      (type == CONTROL_ROTATE && id < STM32_ENCODER_COUNT && (value == -1 || value == 1)))
  {
    enqueue(controls, type, id, value, timestamp_ms);
  }
}

/**
 * @brief   Периодически сообщать получателю, что неподвижная панель работает.
 * @details Разность беззнаковых тиков сохраняет период при переполнении времени.
 */
void stm32_controls_poll(Stm32Controls *controls, uint32_t now_ms)
{
  if ((uint32_t)(now_ms - controls->last_heartbeat_ms) >= 1000U)
  {
    enqueue(controls, CONTROL_HEARTBEAT, 255U, 0, now_ms);
    controls->last_heartbeat_ms = now_ms;
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
