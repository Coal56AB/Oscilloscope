#ifdef NDEBUG
#undef NDEBUG
#endif
#include "controls.h"
#include <assert.h>
#include <string.h>

/**
 * @brief   Прочитать событие через настоящий codec, как его увидит GUI.
 * @details Проверяет пакет общим парсером и только затем подтверждает отправку;
 *          тест не обходит сериализацию прямым чтением внутренней очереди.
 */
static ControlEvent pop(Stm32Controls *c)
{
  uint8_t packet[CONTROL_PACKET_SIZE];
  ControlParser parser;
  ControlEvent e = {0};
  unsigned i;
  int got = 0;
  assert(stm32_controls_peek(c, packet));
  control_parser_init(&parser);
  for (i = 0; i < sizeof(packet); ++i)
  {
    got += control_parser_feed(&parser, packet[i], &e);
  }
  assert(got == 1);
  stm32_controls_sent(c);
  return e;
}

/**
 * @brief   Проверить границу готовых событий, heartbeat и заполнение очереди.
 * @details Аппаратные фильтры проверяются отдельно через настоящую карту GPIO.
 */
int main(void)
{
  Stm32Controls c;
  ControlEvent event;
  unsigned i;
  assert(!stm32_controls_init(NULL));
  assert(stm32_controls_init(&c));
  stm32_controls_input(&c, CONTROL_DOWN, 5, 0, UINT32_MAX - 1);
  event = pop(&c);
  assert(event.type == CONTROL_DOWN && event.control == 5 && event.timestamp_ms == UINT32_MAX - 1);
  stm32_controls_input(&c, CONTROL_UP, 5, 0, 10);
  event = pop(&c);
  assert(event.type == CONTROL_UP);
  stm32_controls_input(&c, CONTROL_ROTATE, 0, 1, 23);
  event = pop(&c);
  assert(event.type == CONTROL_ROTATE && event.value == 1);
  stm32_controls_input(&c, CONTROL_ROTATE, STM32_ENCODER_COUNT, 1, 30);
  stm32_controls_input(&c, CONTROL_ROTATE, 0, 2, 30);
  stm32_controls_input(&c, CONTROL_DOWN, CONTROL_ID_COUNT, 0, 30);
  stm32_controls_input(&c, CONTROL_HEARTBEAT, 0, 0, 30);
  assert(c.count == 0 && c.sequence == 3);
  c.last_heartbeat_ms = UINT32_MAX - 500U;
  stm32_controls_poll(&c, 498U);
  assert(c.count == 0);
  stm32_controls_poll(&c, 499U);
  event = pop(&c);
  assert(event.type == CONTROL_HEARTBEAT && event.timestamp_ms == 499U);
  for (i = 0; i < STM32_TX_EVENTS + 10; ++i)
  {
    stm32_controls_input(&c, CONTROL_ROTATE, 0, -1, i);
  }
  assert(c.count == STM32_TX_EVENTS && c.dropped == 10);
  for (i = 0; i < STM32_TX_EVENTS; ++i)
  {
    event = pop(&c);
    assert(event.value == -1 && event.timestamp_ms == i && event.sequence == i + 4U);
  }
  stm32_controls_input(&c, CONTROL_DOWN, 0, 0, 100);
  event = pop(&c);
  assert(event.sequence == STM32_TX_EVENTS + 14U);
  return 0;
}
