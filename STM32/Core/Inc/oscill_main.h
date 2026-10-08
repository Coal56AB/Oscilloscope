/**
 * @file    oscill_main.h
 * @brief   Точки подключения приложения к сгенерированному main.c.
 */

#ifndef OSCILL_MAIN_H
#define OSCILL_MAIN_H

/** Подготовить панель и UART после инициализации периферии CubeMX. */
void Oscill_Init(void);

/** Выполнить один неблокирующий проход опроса панели и обслуживания UART. */
void Oscill_Process(void);

#endif /* OSCILL_MAIN_H */
