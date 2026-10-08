# STM32 — контроллер панели осциллографа

Прошивка STM32F103RCT6 опрашивает кнопки и энкодеры и передаёт события по UART основному приложению. Сборка прошивки выполняется в Keil.

## Что хранится

- `oscill_controls.ioc` — настройки периферии CubeMX.
- `Core/Oscill/` — приложение контроллера панели.
- `Core/Inc/`, `Core/Src/` — код настройки микроконтроллера.
- `Drivers/` — HAL и CMSIS.
- `MDK-ARM/` — готовый проект Keil и startup.
- `tests/` — проверки логики на ПК.
- `output/` — прошивка и файлы отладки.

## Как собрать прошивку

Подготовка: установить Keil MDK с Arm Compiler 6.19 и пакетом `Keil.STM32F1xx_DFP.2.4.0`.

1. Открыть `MDK-ARM/oscill_controls.uvprojx` в Keil.
2. Выполнить **Project → Rebuild all target files**.
3. Забрать `output/oscill_controls.hex` для прошивки; `output/oscill_controls.axf` — для отладки.

CubeMX 6.12.1 нужен при изменении настроек периферии. Подключение и настройки — [STM32.md](../docs/STM32.md), запись — [FLASHING.md](../docs/FLASHING.md#stm32).
