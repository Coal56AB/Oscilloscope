/** Проверка GPIO-адаптера панели с реальными библиотеками кнопок и энкодеров. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "board_controls.h"
#include "main.h"
#include "general_gpio.h"
#include <assert.h>

/** Отправить один снимок входов при заданном времени. */
static void poll(Stm32Controls *controls, uint32_t tick)
{
  test_hal_tick = tick;
  board_controls_poll(controls, tick);
}

/** Проверить следующую команду, не обходя публичный API подтверждения. */
static ControlEvent pop(Stm32Controls *controls)
{
  uint8_t packet[CONTROL_PACKET_SIZE];
  ControlParser parser;
  ControlEvent event = {0};
  unsigned i;
  int decoded = 0;
  assert(stm32_controls_peek(controls, packet));
  control_parser_init(&parser);
  for (i = 0; i < CONTROL_PACKET_SIZE; ++i)
  {
    decoded += control_parser_feed(&parser, packet[i], &event);
  }
  assert(decoded == 1);
  stm32_controls_sent(controls);
  return event;
}

/** Задать фазы первого энкодера через его действительную распиновку. */
static void phases(unsigned state)
{
  GPIOB->IDR &= ~(CH0_A_Pin | CH0_B_Pin);
  if (state & 2U)
  {
    GPIOB->IDR |= CH0_A_Pin;
  }
  if (state & 1U)
  {
    GPIOB->IDR |= CH0_B_Pin;
  }
}

/** Проверить фильтрацию в момент tick=0, отпускание и независимость кнопок. */
static void test_switch(void)
{
  GPIO_SwitchTypeDef a, b;
  GPIOA->IDR = 0xffffU;
  test_hal_tick = 0;
  assert(GPIO_Switch_Init(&a, GPIOA, GPIO_PIN_0, 0) == HAL_OK);
  assert(GPIO_Switch_Init(&b, GPIOA, GPIO_PIN_1, 0) == HAL_OK);
  a.Sw_FilterDelay = b.Sw_FilterDelay = 5;
  GPIOA->IDR &= ~GPIO_PIN_0;
  assert(GPIO_Read_Switch(&a) == 0);
  test_hal_tick = 5;
  assert(GPIO_Read_Switch(&a) == 1 && GPIO_Read_Switch(&b) == 0);
  GPIOA->IDR |= GPIO_PIN_0;
  assert(GPIO_Read_Switch(&a) == 1);
  test_hal_tick = 8;
  GPIOA->IDR &= ~GPIO_PIN_0;
  assert(GPIO_Read_Switch(&a) == 1);
  test_hal_tick = 9;
  GPIOA->IDR |= GPIO_PIN_0;
  assert(GPIO_Read_Switch(&a) == 1);
  test_hal_tick = 13;
  assert(GPIO_Read_Switch(&a) == 1);
  test_hal_tick = 14;
  assert(GPIO_Read_Switch(&a) == 0);
}

/** Проверить реальные ID, направление, debounce через wrap и полярность LED. */
int main(void)
{
  Stm32Controls controls;
  ControlEvent event;
  unsigned i;
  static const unsigned cw[] = {2, 0, 1, 3};
  static const unsigned ccw[] = {1, 0, 2, 3};
  test_switch();
  GPIOA->IDR = GPIOB->IDR = GPIOC->IDR = GPIOD->IDR = 0xffffU;
  GPIOC->ODR = 0x100U;
  assert(stm32_controls_init(&controls));
  board_controls_init();
  poll(&controls, UINT32_MAX - 4);
  assert(controls.count == 0 && GPIOC->ODR == 0x100U);
  GPIOB->IDR &= ~EN_CH0_Pin;
  poll(&controls, UINT32_MAX - 3);
  GPIOB->IDR |= EN_CH0_Pin;
  poll(&controls, UINT32_MAX - 2);
  GPIOB->IDR &= ~EN_CH0_Pin;
  poll(&controls, UINT32_MAX - 1);
  poll(&controls, 4);
  event = pop(&controls);
  assert(event.type == CONTROL_DOWN && event.control == 5 && event.timestamp_ms == UINT32_MAX - 1);
  GPIOB->IDR |= EN_CH0_Pin;
  poll(&controls, 10);
  poll(&controls, 15);
  event = pop(&controls);
  assert(event.type == CONTROL_UP && event.control == 5 && event.timestamp_ms == 10);
  for (i = 0; i < 4; ++i)
  {
    phases(cw[i]);
    poll(&controls, 20 + i);
  }
  event = pop(&controls);
  assert(event.type == CONTROL_ROTATE && event.control == 0 && event.value == 1);
  for (i = 0; i < 4; ++i)
  {
    phases(ccw[i]);
    poll(&controls, 30 + i);
  }
  event = pop(&controls);
  assert(event.type == CONTROL_ROTATE && event.value == -1);
  phases(0);
  poll(&controls, 40);
  assert(controls.count == 0 && controls.invalid_transitions == 1);
  board_controls_set_run_led(1);
  assert(GPIOC->ODR == (0x100U | LED_RUN_Pin));
  board_controls_set_run_led(0);
  assert(GPIOC->ODR == 0x100U);
  return 0;
}
