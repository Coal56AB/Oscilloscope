/**
 * @file    test_controls_uart.c
 * @brief   Проверка подтверждения, повтора и владения буфером асинхронного UART.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "controls_uart.h"

#include <assert.h>
#include <string.h>

static UART_HandleTypeDef panel_uart = {1U};
static UART_HandleTypeDef other_uart = {2U};
static HAL_StatusTypeDef next_status;
static uint8_t *active_data;
static unsigned starts;
static unsigned aborts;
static unsigned complete_inside_start;
static uint8_t *receive_byte;

/** Предоставить тесту назначенный HAL буфер приёма одного байта. */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size)
{
  assert(uart == &panel_uart && size == 1U);
  receive_byte = data;
  return HAL_OK;
}

/** Имитировать остановку приёма при восстановлении после HAL error. */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
  assert(uart == &panel_uart);
  receive_byte = NULL;
  return HAL_OK;
}

/** Пропустить байты через настоящий callback транспорта. */
static void receive(const uint8_t *bytes, unsigned size)
{
  unsigned i;
  for (i = 0; i < size; ++i)
  {
    assert(receive_byte);
    *receive_byte = bytes[i];
    controls_uart_rx_complete(&panel_uart);
  }
}

/**
 * @brief   Заменить запуск аппаратного TX управляемым результатом HAL.
 * @details Сохраняет именно указатель, а не копию: тест обнаруживает изменение
 *          активного буфера, пока реальный UART ещё мог бы читать его байты.
 */
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size)
{
  assert(uart == &panel_uart);
  assert(size == CONTROL_PACKET_SIZE);
  ++starts;
  if (next_status == HAL_OK)
  {
    active_data = data;
    if (complete_inside_start)
    {
      controls_uart_tx_complete(uart);
    }
  }
  return next_status;
}

/**
 * @brief   Подтвердить, что транспорт отменил HAL перед повтором.
 * @details После отмены тест больше не считает прежний указатель активным.
 */
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
  assert(uart == &panel_uart);
  ++aborts;
  active_data = NULL;
  return HAL_OK;
}

/**
 * @brief   Создать независимый сценарий с одним настоящим событием heartbeat.
 * @details Событие формирует публичный обработчик панели; тест не подменяет
 *          внутренние указатели или счётчики очереди.
 */
static void prepare(Stm32Controls *controls)
{
  assert(stm32_controls_init(controls));
  stm32_controls_poll(controls, 1000U);
  assert(controls->count == 1U);
  controls_uart_init(&panel_uart);
  next_status = HAL_OK;
  active_data = NULL;
  starts = 0U;
  aborts = 0U;
  complete_inside_start = 0U;
}

/**
 * @brief   Проверить занятость UART и сохранность передаваемого пакета.
 * @details Новое нажатие не изменяет активный буфер. Завершение другого UART
 *          не подтверждает событие; правильное завершение продвигает очередь.
 */
static void test_busy_and_completion(void)
{
  Stm32Controls controls;
  uint8_t first_packet[CONTROL_PACKET_SIZE];

  prepare(&controls);
  next_status = HAL_BUSY;
  controls_uart_poll(&controls, 1000U);
  assert(controls.count == 1U && starts == 1U);
  assert(controls_uart_metrics()->start_errors == 1U);
  controls_uart_poll(&controls, 1001U);
  assert(starts == 1U);

  next_status = HAL_OK;
  controls_uart_poll(&controls, 1010U);
  assert(starts == 2U && controls.count == 1U);
  memcpy(first_packet, active_data, sizeof(first_packet));
  stm32_controls_input(&controls, CONTROL_DOWN, 5U, 0, 1011U);
  assert(controls.count == 2U);
  controls_uart_tx_complete(&other_uart);
  controls_uart_error(&other_uart);
  controls_uart_poll(&controls, 1017U);
  assert(controls.count == 2U && starts == 2U && aborts == 0U);
  assert(memcmp(first_packet, active_data, sizeof(first_packet)) == 0);

  controls_uart_tx_complete(&panel_uart);
  controls_uart_poll(&controls, 1020U);
  assert(controls.count == 1U && starts == 3U);
  controls_uart_tx_complete(&panel_uart);
  controls_uart_poll(&controls, 1021U);
  assert(controls.count == 0U && controls_uart_metrics()->packets == 2U);
}

/**
 * @brief   Проверить повтор после ошибки без изменения sequence и данных.
 * @details Частично переданный пакет отменяется, но событие остаётся в очереди
 *          до успешного завершения повторной отправки.
 */
static void test_error_retry(void)
{
  Stm32Controls controls;
  uint8_t original[CONTROL_PACKET_SIZE];

  prepare(&controls);
  controls_uart_poll(&controls, 1000U);
  memcpy(original, active_data, sizeof(original));
  controls_uart_error(&panel_uart);
  controls_uart_poll(&controls, 1001U);
  assert(aborts == 1U && controls.count == 1U);
  assert(controls_uart_metrics()->transfer_errors == 1U);
  controls_uart_poll(&controls, 1010U);
  assert(starts == 1U);
  controls_uart_poll(&controls, 1011U);
  assert(starts == 2U);
  assert(memcmp(original, active_data, sizeof(original)) == 0);
  controls_uart_tx_complete(&panel_uart);
  controls_uart_poll(&controls, 1012U);
  assert(controls.count == 0U);
}

/**
 * @brief   Проверить восстановление отсутствующего IRQ и переполнение tick.
 * @details Уже подтверждённый пакет не считается зависшим даже при позднем
 *          обслуживании. Callback во время запуска HAL тоже не теряется.
 */
static void test_timeout_and_callback_order(void)
{
  Stm32Controls controls;

  prepare(&controls);
  controls_uart_poll(&controls, UINT32_MAX - 20U);
  controls_uart_poll(&controls, 28U);
  assert(aborts == 0U);
  controls_uart_poll(&controls, 29U);
  assert(aborts == 1U && controls.count == 1U);
  assert(controls_uart_metrics()->timeouts == 1U);
  controls_uart_poll(&controls, 39U);
  controls_uart_tx_complete(&panel_uart);
  controls_uart_poll(&controls, 100U);
  assert(controls.count == 0U && aborts == 1U);

  prepare(&controls);
  complete_inside_start = 1U;
  controls_uart_poll(&controls, 1000U);
  controls_uart_poll(&controls, 1001U);
  assert(controls.count == 0U && controls_uart_metrics()->packets == 1U);
}

/**
 * @brief   Выполнить сценарии, которые нельзя проверить одной компиляцией HAL.
 */
static void test_receive_commands(void)
{
  Stm32Controls controls;
  ControlEvent command = {CONTROL_SET_SCALE, 1, 100, 0, 2000};
  ControlEvent received;
  uint8_t packet[CONTROL_PACKET_SIZE];
  uint8_t garbage[128] = {0};
  prepare(&controls);
  assert(control_encode(packet, &command));
  receive(packet, 9);
  assert(!controls_uart_command(&received, 10));
  receive(packet + 9, sizeof(packet) - 9);
  assert(controls_uart_command(&received, 11));
  assert(received.type == CONTROL_SET_SCALE && received.value == 2000 && received.control == 1);
  receive(packet, sizeof(packet));
  assert(!controls_uart_command(&received, 12));
  packet[12] ^= 1;
  receive(packet, sizeof(packet));
  assert(!controls_uart_command(&received, 13));
  command.sequence = 101;
  assert(control_encode(packet, &command));
  receive(packet, sizeof(packet));
  assert(controls_uart_command(&received, 14));
  command.sequence = 0;
  assert(control_encode(packet, &command));
  receive(packet, sizeof(packet));
  assert(!controls_uart_command(&received, 15));
  receive(packet, sizeof(packet));
  assert(controls_uart_command(&received, 2014));
  receive(garbage, sizeof(garbage));
  assert(!controls_uart_command(&received, 2015));
  assert(controls_uart_metrics()->rx_overflows == 1);
  receive(packet, sizeof(packet));
  assert(controls_uart_command(&received, 2016));
  controls_uart_error(&panel_uart);
  assert(!controls_uart_command(&received, 2017));
  controls_uart_poll(&controls, 2017);
  receive(packet, sizeof(packet));
  assert(controls_uart_command(&received, 2018));
  assert(controls_uart_metrics()->rx_errors == 1);
}

/** Выполнить сценарии передачи и приёма с независимым восстановлением. */
int main(void)
{
  test_busy_and_completion();
  test_error_retry();
  test_timeout_and_callback_order();
  test_receive_commands();
  return 0;
}
