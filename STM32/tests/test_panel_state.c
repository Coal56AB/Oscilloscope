/** @file test_panel_state.c Проверки масштаба, переключения и автономной отладки. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "panel_state.h"
#include "oscill_config.h"
#include <assert.h>

static uint8_t gpio_mask;
static unsigned writes;

/** Принять выходы вместо GPIO; проверяется вызов реального автомата панели. */
static void write_relays(uint8_t mask)
{
  gpio_mask = mask;
  ++writes;
}

/** Прочитать выход очереди через общий кодек, не обходя сериализацию. */
static ControlEvent pop(Stm32Controls *events)
{
  ControlParser parser;
  ControlEvent event = {0};
  uint8_t packet[CONTROL_PACKET_SIZE];
  unsigned i;
  int count = 0;
  assert(stm32_controls_peek(events, packet));
  control_parser_init(&parser);
  for (i = 0; i < sizeof(packet); ++i)
  {
    count += control_parser_feed(&parser, packet[i], &event);
  }
  assert(count == 1);
  stm32_controls_sent(events);
  return event;
}

/** Проверить реальные границы шкалы, запросы GUI, паузу и сохранение прочих событий. */
int main(void)
{
  Stm32Controls events;
  PanelState panel;
  ControlEvent command = {CONTROL_SET_SCALE, 0, 0, 0, 5000};
  ControlEvent event;
  unsigned i;
  assert(stm32_controls_init(&events));
  panel_state_init(&panel, &events);
  panel_state_poll(&panel, UINT32_MAX - 5U, write_relays);
  assert(writes == 1 && gpio_mask == 54U && events.count == 0);
  panel_state_poll(&panel, 3U, write_relays);
  assert(events.count == 0);
  panel_state_poll(&panel, 4U, write_relays);
  event = pop(&events);
  assert(event.type == CONTROL_SCALE && event.control == 0 && event.value == 500);
  event = pop(&events);
  assert(event.type == CONTROL_SCALE && event.control == 1 && event.value == 1000);

  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, 5U);
  assert(events.count == 0 && panel.scale[0] == 3);
  panel_state_poll(&panel, 5U, write_relays);
  assert(writes == 1);
  assert(pop(&events).value == 1000);
  panel_state_command(&panel, &command, 6U);
  panel_state_poll(&panel, 6U, write_relays);
  assert(gpio_mask == 52U && events.count == 0);
  panel_state_input(&panel, CONTROL_ROTATE, 0, -1, 7U);
  panel_state_poll(&panel, 7U, write_relays);
  assert(gpio_mask == 54U);
  panel_state_poll(&panel, 16U, write_relays);
  assert(events.count == 0);
  panel_state_poll(&panel, 17U, write_relays);
  assert(pop(&events).value == 2000);
  command.value = 1234;
  panel_state_command(&panel, &command, 18U);
  assert(panel.scale[0] == 4);
  for (i = 0; i < 10; ++i)
  {
    panel_state_input(&panel, CONTROL_ROTATE, 0, -1, 20U);
  }
  assert(panel.scale[0] == 0);
  panel_state_poll(&panel, 20U, write_relays);
  panel_state_poll(&panel, 30U, write_relays);
  assert(gpio_mask == 55U && pop(&events).value == 100);
  panel_state_poll(&panel, 1030U, write_relays);
  assert(events.count == 2);
  assert(pop(&events).value == 100 && pop(&events).value == 1000);

  command.type = CONTROL_SET_ENCODER_MODE;
  command.value = 1;
  panel_state_command(&panel, &command, 1030U);
  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, 1031U);
  assert(panel.scale[0] == 0 && pop(&events).type == CONTROL_ROTATE);
  panel_state_input(&panel, CONTROL_ROTATE, 2, 1, 1032U);
  assert(pop(&events).control == 2);
  command.type = CONTROL_SET_RUN;
  command.control = 255;
  panel_state_command(&panel, &command, 1032U);
  assert(panel.run == 1);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 1033U);
  assert(panel.run == 1);
  assert(pop(&events).type == CONTROL_DOWN);
  panel_state_input(&panel, CONTROL_UP, 10, 0, 1040U);
  assert(pop(&events).type == CONTROL_UP && panel.run == 1);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 1050U);
  assert(panel.run == 1);
  assert(pop(&events).type == CONTROL_DOWN);

  /* At the timeout boundary debug resumes local controls, including across wrap. */
  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, 3031U);
  assert(panel.scale[0] == 0 && pop(&events).type == CONTROL_ROTATE);
  command.value = 1234; /* Invalid commands must not extend host liveness. */
  panel_state_command(&panel, &command, 3031U);
  panel_state_input(&panel, CONTROL_ROTATE, 0, 1, 3032U);
  if (STM32_STANDALONE_DEBUG)
  {
    assert(panel.scale[0] == 1 && events.count == 0);
  }
  else
  {
    assert(panel.scale[0] == 0 && pop(&events).type == CONTROL_ROTATE);
  }
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 3032U);
  assert(panel.run == !STM32_STANDALONE_DEBUG);
  assert(pop(&events).type == CONTROL_DOWN);
  command.value = 1;
  panel_state_command(&panel, &command, UINT32_MAX - 1000U);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 998U);
  assert(panel.run == 1);
  assert(pop(&events).type == CONTROL_DOWN);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 999U);
  assert(panel.run == !STM32_STANDALONE_DEBUG);
  assert(pop(&events).type == CONTROL_DOWN);
  command.value = 0;
  panel_state_command(&panel, &command, 1000U);
  assert(panel.run == 0);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 1001U);
  assert(panel.run == 0 && pop(&events).type == CONTROL_DOWN);

  /* A panel started with no GUI is immediately usable in debug mode. */
  panel_state_init(&panel, &events);
  panel_state_input(&panel, CONTROL_DOWN, 10, 0, 0U);
  assert(panel.run == STM32_STANDALONE_DEBUG);
  assert(pop(&events).type == CONTROL_DOWN);
  assert(panel_state_power_mv(0) == 0);
  assert(panel_state_power_mv(2048) == 3301);
  assert(panel_state_power_mv(4095) == 6600);
  return 0;
}
