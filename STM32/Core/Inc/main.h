/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EN_CH1_Pin GPIO_PIN_10
#define EN_CH1_GPIO_Port GPIOB
#define EN_CH0_Pin GPIO_PIN_11
#define EN_CH0_GPIO_Port GPIOB
#define TRG_MODE_Pin GPIO_PIN_12
#define TRG_MODE_GPIO_Port GPIOB
#define CURSOR_Pin GPIO_PIN_13
#define CURSOR_GPIO_Port GPIOB
#define RUN_Pin GPIO_PIN_14
#define RUN_GPIO_Port GPIOB
#define MENU_Pin GPIO_PIN_15
#define MENU_GPIO_Port GPIOB
#define FUNC_A_Pin GPIO_PIN_8
#define FUNC_A_GPIO_Port GPIOC
#define FUNC_B_Pin GPIO_PIN_9
#define FUNC_B_GPIO_Port GPIOC
#define FUNC_ENC_Pin GPIO_PIN_8
#define FUNC_ENC_GPIO_Port GPIOA
#define TRG_B_Pin GPIO_PIN_15
#define TRG_B_GPIO_Port GPIOA
#define TRG_A_Pin GPIO_PIN_10
#define TRG_A_GPIO_Port GPIOC
#define TRG_ENC_Pin GPIO_PIN_11
#define TRG_ENC_GPIO_Port GPIOC
#define TIME_B_Pin GPIO_PIN_12
#define TIME_B_GPIO_Port GPIOC
#define TIME_A_Pin GPIO_PIN_2
#define TIME_A_GPIO_Port GPIOD
#define TIME_ENC_Pin GPIO_PIN_3
#define TIME_ENC_GPIO_Port GPIOB
#define CH1_B_Pin GPIO_PIN_4
#define CH1_B_GPIO_Port GPIOB
#define CH1_A_Pin GPIO_PIN_5
#define CH1_A_GPIO_Port GPIOB
#define CH1_ENC_Pin GPIO_PIN_6
#define CH1_ENC_GPIO_Port GPIOB
#define CH0_B_Pin GPIO_PIN_7
#define CH0_B_GPIO_Port GPIOB
#define CH0_A_Pin GPIO_PIN_8
#define CH0_A_GPIO_Port GPIOB
#define CH0_ENC_Pin GPIO_PIN_9
#define CH0_ENC_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
