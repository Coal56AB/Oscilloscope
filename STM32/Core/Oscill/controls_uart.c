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
#define UART_RX_BYTES 128U
static uint8_t rx_byte;
static uint8_t rx_data[UART_RX_BYTES];
static volatile unsigned rx_head;
static volatile unsigned rx_tail;
static volatile uint8_t rx_armed;
static volatile uint8_t rx_failed;
static volatile uint8_t rx_overflow;
static ControlParser rx_parser;
static uint32_t last_command_ms;

/**
 * @brief   Перевзвести побайтовый RX, не меняя очередь приложения.
 * @details HAL вызывает завершение после освобождения RxState; при ошибке
 *          повтор произойдёт из главного цикла. TX имеет независимое состояние.
 */
static void start_receive(void)
{
  rx_armed = HAL_UART_Receive_IT(control_uart, &rx_byte, 1U) == HAL_OK;
}

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
  rx_head = rx_tail = 0U;
  rx_failed = rx_overflow = rx_armed = 0U;
  last_command_ms = 0U;
  control_parser_init(&rx_parser);
  if (uart != NULL)
  {
    start_receive();
  }
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
  if (rx_failed)
  {
    HAL_UART_AbortReceive(control_uart);
    rx_failed = 0U;
    rx_armed = 0U;
    rx_tail = rx_head;
    control_parser_init(&rx_parser);
    ++metrics.rx_errors;
  }
  if (!rx_armed)
  {
    start_receive();
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
    rx_failed = 1U;
  }
}

/**
 * @brief   Перенести байт из HAL в ограниченный SPSC буфер.
 * @details IRQ изменяет только head; tail принадлежит главному циклу.
 *          Барьер публикует содержимое до head. Переполнение отменяет фрагмент.
 */
void controls_uart_rx_complete(UART_HandleTypeDef *uart)
{
  if (uart == control_uart)
  {
    unsigned next = (rx_head + 1U) % UART_RX_BYTES;
    if (next == rx_tail)
    {
      rx_overflow = 1U;
    }
    else
    {
      rx_data[rx_head] = rx_byte;
      __DMB();
      rx_head = next;
    }
    start_receive();
  }
}

/**
 * @brief   Разобрать команду вне IRQ с проверкой CRC и sequence.
 * @details После 2 с без принятых пакетов разрешается новая эпоха отправителя,
 *          поэтому перезапуск GUI не требует перезапуска STM32.
 */
int controls_uart_command(ControlEvent *command, uint32_t now_ms)
{
  if (rx_failed)
  {
    return 0;
  }
  if (rx_overflow)
  {
    rx_overflow = 0U;
    rx_tail = rx_head;
    control_parser_init(&rx_parser);
    ++metrics.rx_overflows;
  }
  if (rx_parser.have_sequence && (uint32_t)(now_ms - last_command_ms) >= 2000U)
  {
    control_parser_init(&rx_parser);
  }
  while (rx_tail != rx_head)
  {
    uint8_t byte;
    __DMB();
    byte = rx_data[rx_tail];
    __DMB();
    rx_tail = (rx_tail + 1U) % UART_RX_BYTES;
    if (control_parser_feed(&rx_parser, byte, command))
    {
      last_command_ms = now_ms;
      ++metrics.received;
      return 1;
    }
  }
  return 0;
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
