/** @file oscill_config.h Настройки приложения панели, общие для всех её модулей. */
#ifndef OSCILL_CONFIG_H
#define OSCILL_CONFIG_H

/* 1: без связи с GUI включается автономная отладка; RUN управляет местным LED.
   Задать здесь или через Define компилятора STM32_STANDALONE_DEBUG=1. */
#ifndef STM32_STANDALONE_DEBUG
#define STM32_STANDALONE_DEBUG 0
#endif
#if STM32_STANDALONE_DEBUG != 0 && STM32_STANDALONE_DEBUG != 1
#error STM32_STANDALONE_DEBUG must be 0 or 1
#endif

#define STM32_RELAY_SETTLE_MS 10U
/* Номинальная опора ADC; для калибровки задать измеренное VREF+ в мВ. */
#define STM32_ADC_REFERENCE_MV 3300U
#if STM32_ADC_REFERENCE_MV < 2400U || STM32_ADC_REFERENCE_MV > 3600U
#error STM32_ADC_REFERENCE_MV must be within the supported ADC reference range
#endif
#endif
