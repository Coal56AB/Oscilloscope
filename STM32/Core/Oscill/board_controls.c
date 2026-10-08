/**
 * @file    board_controls.c
 * @brief   Единственное соответствие аппаратных входов идентификаторам GUI.
 * @details Имена GPIO генерирует CubeMX из oscill_controls.ioc. Цепи CH0/CH1
 *          на схеме соответствуют CH1/CH2 в пользовательском интерфейсе.
 */

#include "board_controls.h"
#include "main.h"
#include "general_gpio.h"
#include "general_encoder.h"

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

static GPIO_SwitchTypeDef switches[CONTROL_ID_COUNT];
static Encoder_HandleTypeDef encoders[STM32_ENCODER_COUNT];
static GPIO_LEDTypeDef run_led;

/**
 * @brief   Привязать общие обработчики кнопок, энкодеров и LED к плате.
 * @details Вызывать после MX_GPIO_Init. Исходные уровни принимаются без событий,
 *          LED_RUN на PC7 начинает работу выключенным.
 */
void board_controls_init(void)
{
  unsigned i;
  for (i = 0; i < CONTROL_ID_COUNT; ++i)
  {
    if (GPIO_Switch_Init(&switches[i], button_inputs[i].port, button_inputs[i].pin, 0U) != HAL_OK)
    {
      Error_Handler();
    }
    switches[i].Sw_FilterDelay = 5U;
  }
  for (i = 0; i < STM32_ENCODER_COUNT; ++i)
  {
    if (Encoder_Init(&encoders[i], encoder_inputs[i][0].port, encoder_inputs[i][0].pin,
                     encoder_inputs[i][1].port, encoder_inputs[i][1].pin, 4U) != HAL_OK)
    {
      Error_Handler();
    }
  }
  if (GPIO_LED_Init(&run_led, LED_RUN_GPIO_Port, LED_RUN_Pin, 1U) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief   Передать готовые изменения панели в очередь приложения.
 * @details Общая библиотека владеет debounce и декодером фаз. Время DOWN/UP
 *          берётся до фильтрации; направление и детенты не вычисляются повторно.
 */
void board_controls_poll(Stm32Controls *controls, uint32_t now_ms)
{
  unsigned i;
  for (i = 0; i < CONTROL_ID_COUNT; ++i)
  {
    uint32_t previous = switches[i].Sw_CurrentState;
    int pressed = GPIO_Read_Switch(&switches[i]);
    if (pressed >= 0 && (uint32_t)pressed != previous)
    {
      stm32_controls_input(controls, pressed ? CONTROL_DOWN : CONTROL_UP, i, 0,
                           switches[i].tickprev);
    }
  }
  for (i = 0; i < STM32_ENCODER_COUNT; ++i)
  {
    uint32_t invalid = encoders[i].invalid_transitions;
    int step = Encoder_Update(&encoders[i]);
    controls->invalid_transitions += (uint32_t)(encoders[i].invalid_transitions - invalid);
    if (step)
    {
      stm32_controls_input(controls, CONTROL_ROTATE, i, step, now_ms);
    }
  }
  GPIO_LED_Dynamic_Handle(&run_led);
}

/**
 * @brief   Выставить индикатор RUN через общую библиотеку светодиодов.
 * @details Состояние задаёт вызывающий код; нажатие кнопки не подменяет состояние GUI.
 */
void board_controls_set_run_led(uint8_t enabled)
{
  GPIO_LED_Set(&run_led, enabled);
}
