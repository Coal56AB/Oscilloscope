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
/* Программная граница полного шага A/B=11: оба контакта разомкнуты при pull-up.
   Это настройка декодера, а не измерение установленного исполнения энкодера. */
#ifndef STM32_ENCODER_DETENT_PHASE
#define STM32_ENCODER_DETENT_PHASE 3U
#endif
#if STM32_ENCODER_DETENT_PHASE > 3U
#error STM32_ENCODER_DETENT_PHASE must be between 0 and 3
#endif
/* Номинальная опора ADC; для калибровки задать измеренное VREF+ в мВ. */
#define STM32_ADC_REFERENCE_MV 3300U
/* Типовые DS5792, не индивидуальная калибровка установленного кристалла. */
#ifndef STM32_VREFINT_MV
#define STM32_VREFINT_MV 1200U
#endif
#ifndef STM32_TEMPERATURE_V25_MV
#define STM32_TEMPERATURE_V25_MV 1430U
#endif
#ifndef STM32_TEMPERATURE_SLOPE_UV_PER_C
#define STM32_TEMPERATURE_SLOPE_UV_PER_C 4300U
#endif
#if STM32_VREFINT_MV == 0 || STM32_TEMPERATURE_V25_MV == 0 || STM32_TEMPERATURE_SLOPE_UV_PER_C == 0
#error Temperature conversion coefficients must be positive
#endif
#if STM32_ADC_REFERENCE_MV < 2400U || STM32_ADC_REFERENCE_MV > 3600U
#error STM32_ADC_REFERENCE_MV must be within the supported ADC reference range
#endif
#endif
