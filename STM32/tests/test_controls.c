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
 * @brief   Проверить поведение панели без зависимости от GPIO конкретной платы.
 * @details Сценарии включают дребезг, переполнение времени, вращение,
 *          недопустимые переходы фаз и заполнение очереди событий.
 */
int main(void)
{
  Stm32Controls c;
  uint8_t b[CONTROL_ID_COUNT] = {0}, e[5] = {0};
  ControlEvent event;
  unsigned i;
  assert(stm32_controls_init(&c, b, e, 4, 5));
  b[5] = 1;
  stm32_controls_scan(&c, b, e, UINT32_MAX - 3);
  b[5] = 0;
  stm32_controls_scan(&c, b, e, UINT32_MAX - 2);
  b[5] = 1;
  stm32_controls_scan(&c, b, e, UINT32_MAX - 1);
  stm32_controls_scan(&c, b, e, 4);
  while (c.count)
  {
    event = pop(&c);
    if (event.type == CONTROL_DOWN)
    {
      break;
    }
  }
  assert(event.type == CONTROL_DOWN && event.control == 5 && event.timestamp_ms == UINT32_MAX - 1);
  b[5] = 0;
  stm32_controls_scan(&c, b, e, 10);
  stm32_controls_scan(&c, b, e, 15);
  event = pop(&c);
  assert(event.type == CONTROL_UP);
  for (i = 0; i < 4; ++i)
  {
    static const uint8_t q[] = {1, 3, 2, 0};
    e[0] = q[i];
    stm32_controls_scan(&c, b, e, 20 + i);
  }
  event = pop(&c);
  assert(event.type == CONTROL_ROTATE && event.value == 1);
  e[0] = 3;
  stm32_controls_scan(&c, b, e, 30);
  assert(c.invalid_transitions == 1);
  for (i = 0; i < STM32_TX_EVENTS + 10; ++i)
  {
    stm32_controls_scan(&c, b, e, 1000 * (i + 1));
  }
  assert(c.count == STM32_TX_EVENTS && c.dropped == 10);
  return 0;
}
