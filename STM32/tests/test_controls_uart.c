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
int main(void)
{
  test_busy_and_completion();
  test_error_retry();
  test_timeout_and_callback_order();
  return 0;
}
