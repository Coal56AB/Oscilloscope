/**
 * @file    controls_uart.c
 * @brief   Передача очереди панели с подтверждением завершения и повтором ошибок.
 */

#include "controls_uart.h"

#include <string.h>

/* Пакет 20 байт при 115200 8N1 занимает около 1,74 мс. Таймаут нужен
   для восстановления зависшего HAL/IRQ, а не для штатного ожидания TX. */
#define UART_TX_TIMEOUT_MS 50U
#define UART_RETRY_MS 10U

static UART_HandleTypeDef *control_uart;
static uint8_t tx_packet[CONTROL_PACKET_SIZE];
static volatile uint8_t tx_complete;
static volatile uint8_t tx_error;
static uint8_t tx_active;
static uint8_t retry_pending;
static uint32_t tx_started_ms;
static uint32_t retry_started_ms;
static ControlsUartMetrics metrics;

/**
 * @brief   Подготовить транспорт к работе с единственным UART панели.
 * @details Привязывает предварительно настроенный HAL handle. Собственный
 *          буфер остаётся неизменным до завершения передачи или её отмены.
 * @note    Вызывать один раз после MX_USART1_UART_Init, до запуска опроса.
 */
void controls_uart_init(UART_HandleTypeDef *uart)
{
  control_uart = uart;
  tx_complete = 0U;
  tx_error = 0U;
  tx_active = 0U;
  retry_pending = 0U;
  memset(&metrics, 0, sizeof(metrics));
}

/**
 * @brief   Продвинуть передачу без блокировки опроса панели.
 * @details Событие удаляется только после HAL TX complete. Ошибка или таймаут
 *          отменяет передачу и оставляет событие для повторной попытки с тем
 *          же sequence. IRQ публикует только флаги и не владеет очередью.
 * @note    Частично отправленный пакет при повторе заново начинается с magic;
 *          общий парсер GUI восстанавливает синхронизацию и отсеивает дубли.
 */
void controls_uart_poll(Stm32Controls *controls, uint32_t now_ms)
{
  HAL_StatusTypeDef status;

  if (control_uart == NULL)
  {
    return;
  }

  if (tx_active)
  {
    if (tx_error || (!tx_complete && (uint32_t)(now_ms - tx_started_ms) >= UART_TX_TIMEOUT_MS))
    {
      if (tx_error)
      {
        ++metrics.transfer_errors;
      }
      else
      {
        ++metrics.timeouts;
      }

      /* Сначала остановить HAL, затем разрешить переиспользование буфера. */
      HAL_UART_AbortTransmit(control_uart);
      tx_active = 0U;
      tx_complete = 0U;
      tx_error = 0U;
      retry_pending = 1U;
      retry_started_ms = now_ms;
    }
    else if (tx_complete)
    {
      stm32_controls_sent(controls);
      ++metrics.packets;
      tx_active = 0U;
    }
    else
    {
      return;
    }
  }

  if (retry_pending && (uint32_t)(now_ms - retry_started_ms) < UART_RETRY_MS)
  {
    return;
  }
  retry_pending = 0U;

  if (!stm32_controls_peek(controls, tx_packet))
  {
    return;
  }

  /* Флаги очищаются до разрешения IRQ внутри HAL_UART_Transmit_IT. */
  tx_complete = 0U;
  tx_error = 0U;
  tx_started_ms = now_ms;
  tx_active = 1U;
  status = HAL_UART_Transmit_IT(control_uart, tx_packet, sizeof(tx_packet));
  if (status != HAL_OK)
  {
    ++metrics.start_errors;
    tx_active = 0U;
    retry_pending = 1U;
    retry_started_ms = now_ms;
  }
}

/**
 * @brief   Сообщить главному циклу, что HAL закончил отправку пакета.
 * @details Фильтр handle не позволяет завершению другого UART подтвердить
 *          событие панели. Очередь освобождается позднее в poll.
 */
void controls_uart_tx_complete(UART_HandleTypeDef *uart)
{
  if (uart == control_uart)
  {
    tx_complete = 1U;
  }
}

/**
 * @brief   Запросить восстановление транспорта после ошибки HAL.
 * @details В IRQ не выполняются отмена передачи, задержки или работа
 *          с очередью. Повтор с сохранённым событием выполняет poll.
 */
void controls_uart_error(UART_HandleTypeDef *uart)
{
  if (uart == control_uart)
  {
    tx_error = 1U;
  }
}

/**
 * @brief   Предоставить результаты работы транспорта для диагностики.
 * @details Структура доступна для чтения из главного цикла и отладчика;
 *          callbacks её не меняют. Повторная инициализация обнуляет счётчики.
 */
const ControlsUartMetrics *controls_uart_metrics(void)
{
  return &metrics;
}
