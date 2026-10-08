/**
 * @file    oscill_main.c
 * @brief   Приложение контроллера панели осциллографа.
 * @details GPIO, алгоритм органов управления и транспорт разделены. main.c
 *          содержит только вызовы этого API в сохраняемых секциях USER CODE.
 */

#include "oscill_main.h"
#include "board_controls.h"
#include "controls_uart.h"
#include "usart.h"

static Stm32Controls controls;
static uint32_t last_scan_ms;

/**
 * @brief   Начать работу панели с уже настроенной периферией.
 * @details Снимает исходные состояния контактов и передаёт их декодеру, чтобы
 *          питание не создало ложного шага. Назначает USART1 для отправки
 *          событий и запоминает начало миллисекундного опроса.
 * @note    Вызывать из USER CODE 2 после MX_GPIO_Init и MX_USART1_UART_Init.
 */
void Oscill_Init(void)
{
  if (!stm32_controls_init(&controls))
  {
    Error_Handler();
  }

  board_controls_init();
  controls_uart_init(&huart1);
  last_scan_ms = HAL_GetTick();
}

/**
 * @brief   Обслужить панель, не задерживая обработку следующих контактов.
 * @details Новый снимок обрабатывается при смене миллисекундного тика.
 *          UART обслуживается при каждом проходе, независимо от новых нажатий.
 *          Пропущенные интервалы не воспроизводятся вымышленными снимками GPIO.
 * @note    Вызывать непрерывно из USER CODE 3 главного цикла.
 */
void Oscill_Process(void)
{
  uint32_t now_ms = HAL_GetTick();

  if (now_ms != last_scan_ms)
  {
    last_scan_ms = now_ms;
    board_controls_poll(&controls, now_ms);
    stm32_controls_poll(&controls, now_ms);
  }

  controls_uart_poll(&controls, now_ms);
}

/**
 * @brief   Передать завершение HAL TX транспорту панели.
 * @details Этот callback вызывается HAL из USART IRQ. Он только публикует
 *          результат; подтверждение события выполняет главный цикл.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  controls_uart_tx_complete(uart);
}

/**
 * @brief   Передать ошибку HAL транспорту для последующего восстановления.
 * @details Работа с очередью и повтор передачи остаются вне IRQ, чтобы
 *          не повредить события, которые в это время обрабатывает главный цикл.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  controls_uart_error(uart);
}
