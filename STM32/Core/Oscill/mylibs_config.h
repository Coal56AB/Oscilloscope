/**
 * @file    mylibs_config.h
 * @brief   Привязка общих библиотек к HAL контроллера панели.
 */
#ifndef OSCILL_MYLIBS_CONFIG_H
#define OSCILL_MYLIBS_CONFIG_H
#include "stm32f1xx_hal.h"
#define local_time() HAL_GetTick()
#endif
