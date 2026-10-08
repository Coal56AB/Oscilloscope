/**
 * @file    board_controls.c
 * @brief   Единственное соответствие аппаратных входов идентификаторам GUI.
 * @details Имена GPIO генерирует CubeMX из oscill_controls.ioc. Цепи CH0/CH1
 *          на схеме соответствуют CH1/CH2 в пользовательском интерфейсе.
 */

#include "board_controls.h"
#include "main.h"

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} BoardInput;

static const BoardInput button_inputs[CONTROL_ID_COUNT] = {{CH0_ENC_GPIO_Port, CH0_ENC_Pin},
                                                           {CH1_ENC_GPIO_Port, CH1_ENC_Pin},
                                                           {TIME_ENC_GPIO_Port, TIME_ENC_Pin},
                                                           {TRG_ENC_GPIO_Port, TRG_ENC_Pin},
                                                           {FUNC_ENC_GPIO_Port, FUNC_ENC_Pin},
                                                           {EN_CH0_GPIO_Port, EN_CH0_Pin},
                                                           {EN_CH1_GPIO_Port, EN_CH1_Pin},
                                                           {CURSOR_GPIO_Port, CURSOR_Pin},
                                                           {TRG_MODE_GPIO_Port, TRG_MODE_Pin},
                                                           {MENU_GPIO_Port, MENU_Pin},
                                                           {RUN_GPIO_Port, RUN_Pin}};

static const BoardInput encoder_inputs[STM32_ENCODER_COUNT][2] = {
    {{CH0_A_GPIO_Port, CH0_A_Pin}, {CH0_B_GPIO_Port, CH0_B_Pin}},
    {{CH1_A_GPIO_Port, CH1_A_Pin}, {CH1_B_GPIO_Port, CH1_B_Pin}},
    {{TIME_A_GPIO_Port, TIME_A_Pin}, {TIME_B_GPIO_Port, TIME_B_Pin}},
    {{TRG_A_GPIO_Port, TRG_A_Pin}, {TRG_B_GPIO_Port, TRG_B_Pin}},
    {{FUNC_A_GPIO_Port, FUNC_A_Pin}, {FUNC_B_GPIO_Port, FUNC_B_Pin}}};

/**
 * @brief   Снять электрический уровень входа без изменения его назначения.
 * @details Нормализация кнопок и объединение фаз выполняются отдельно, чтобы
 *          полярность кнопки не влияла на направление декодирования энкодера.
 */
static uint8_t read_level(const BoardInput *input)
{
  return HAL_GPIO_ReadPin(input->port, input->pin) == GPIO_PIN_SET;
}

/**
 * @brief   Подготовить снимок панели для переносимого обработчика.
 * @details Контакты кнопок и общие контакты энкодеров соединены с GND.
 *          Кнопка возвращает 1 при нажатии; фазы сохраняют электрические
 *          уровни, A в старшем бите. Дребезг и детенты обрабатываются выше.
 * @note    Вызывать после MX_GPIO_Init, только из главного цикла.
 */
void board_controls_read(uint8_t buttons[CONTROL_ID_COUNT], uint8_t encoders[STM32_ENCODER_COUNT])
{
  unsigned i;

  for (i = 0; i < CONTROL_ID_COUNT; ++i)
  {
    buttons[i] = !read_level(&button_inputs[i]);
  }

  for (i = 0; i < STM32_ENCODER_COUNT; ++i)
  {
    encoders[i] =
        (uint8_t)((read_level(&encoder_inputs[i][0]) << 1U) | read_level(&encoder_inputs[i][1]));
  }
}
