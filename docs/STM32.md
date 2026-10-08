# Контроллер панели STM32F103RCT6

## Проект

`STM32/oscill_controls.ioc` — конфигурация CubeMX 6.12.1, STM32CubeF1 1.8.7.
`STM32/MDK-ARM/oscill_controls.uvprojx` — проект Keil MDK-ARM с Arm Compiler 6.
Выбран STM32F103RCT6: Flash 256 КиБ, SRAM 48 КиБ. Startup называется
`startup_stm32f103xe.s`: ST использует этот файл для семейства F103xC/xD/xE.

```text
STM32/
├── oscill_controls.ioc
├── Core/
│   ├── Inc/        заголовки приложения и генерируемой периферии
│   └── Src/        приложение и генерируемая периферия
├── Drivers/        локальные HAL/CMSIS с лицензиями ST/Arm
├── MDK-ARM/        проект Keil и startup
└── tests/          переносимые проверки панели и UART
```

Структура и оформление прикладного C-кода следуют образцу UIPS. Периферию
настраивает CubeMX; `board_controls.c` читает плату, `controls.c` обрабатывает
контакты, `controls_uart.c` отправляет события, `oscill_main.c` связывает модули.
Общий codec включён из `ZYNQ7020/gui/src/control_protocol.c`, без копирования.

## Настройки CubeMX

- HSI 8 МГц → HSI/2 × PLL16 → SYSCLK/HCLK 64 МГц; APB1 32 МГц, APB2 64 МГц.
- SysTick 1 мс; входы панели — GPIO input с pull-up.
- SYS Debug — Serial Wire: PA13/PA14 доступны для SWD; PB3/PB4/PA15 освобождены от JTAG.
- USART1 PA9/PA10, 115200 8N1, без flow control; IRQ priority 2. TX через HAL IT.
- `Generate peripheral initialization as a pair of .c/.h files`, `Keep User Code` включены.
- HAL/CMSIS копируются в проект; сборка не обращается к соседним рабочим копиям.

Внутренний HSI выбран для первого запуска: частота кварца установленного модуля
не установлена. 64 МГц — настройка проекта, а не измеренная частота.

## Входы панели

Источник — символ модуля и подписи цепей `PCB/AnalExtend/Interfaces.SchDoc`
в PCB commit `5c4f6dd616d461db2b3ec94a01ae66af5781a7e2`.
Номера GPIO относятся к STM32, не к порядку контактов PBD.

| Назначение GUI | Цепи на схеме | A | B | Кнопка |
|---|---|---|---|---|
| CH1 | CH0 | PB8 | PB7 | PB9 |
| CH2 | CH1 | PB5 | PB4 | PB6 |
| TIME | TIME | PD2 | PC12 | PB3 |
| TRIGGER | TRG | PC10 | PA15 | PC11 |
| FUNCTION | FUNC | PC8 | PC9 | PA8 |

| Кнопка GUI | Цепь | GPIO |
|---|---|---|
| CH1 | EN_CH0 | PB11 |
| CH2 | EN_CH1 | PB10 |
| CURSOR | CURSOR | PB13 |
| MODE | TRG_MODE | PB12 |
| MENU | MENU | PB15 |
| RUN | RUN | PB14 |

Контакты замыкаются на GND; драйвер нормализует LOW в «нажато». Настройки первого
запуска: debounce 5 мс, четыре перехода фаз на детент, опрос при смене SysTick.
Направление вращения, переходы на детент и дребезг проверить на реальных энкодерах.
Реле, измерение питания и LED_RUN этим приложением пока не управляются.

## UART и запуск

PA9/PA10 свободны на схеме: это выбранный интерфейс для отдельных проводов,
а не существующее соединение с Zynq. PA9 — TX, PA10 — RX; уровни 3,3 В,
общий GND. Для первого теста подключить PA9 к RX USB–UART 3,3 В и GND.
PA10 можно оставить неподключённым: приложение отправляет события и heartbeat,
приём команд не реализован. Формат — [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md).

Для GUI использовать соответствующий serial device и `--baud 115200`.
Назначенный UART1 Zynq занят загрузочной консолью; проводной канал к STM32
требует отдельного согласованного подключения, автоматическая замена консоли не выполнена.

## Генерация и сборка

Открыть `.ioc`, оставить `MDK-ARM V5.32` и выполнить Generate Code. Затем открыть
`.uvprojx` в Keil и выполнить Rebuild. Результаты Keil направлены в `output/stm32/`
в корне репозитория: `oscill_controls.axf`, `oscill_controls.hex` и map/listing.

Прикладные файлы находятся в группах `Application/Panel` и `Shared/Control protocol`.
Сгенерированный `main.c` подключает `oscill_main.h` в `USER CODE Includes`,
вызывает `Oscill_Init()` в `USER CODE 2`, `Oscill_Process()` в `USER CODE 3`.
Изменения приложения не требуют правки генерируемого кода вне этих секций.
Callbacks UART находятся в отдельном `oscill_main.c`.

После генерации выполнить `python STM32/tests/check_project.py`, затем Rebuild.
Проверка обнаруживает потерю модулей, include-путей, точек подключения, настроек
MCU/SWD/UART и выходного каталога. Она не заменяет сборку или проверку платы.
Реальная повторная генерация проверена отдельно; результаты — [TESTING.md](TESTING.md).

## Отправка и восстановление

Главный цикл — единственный владелец очереди событий. HAL UART IRQ публикует
флаги; TX complete подтверждает событие. Пока HAL читает пакет, буфер не меняется.
При ошибке или отсутствии завершения 50 мс передача отменяется и повторяется
через 10 мс с тем же sequence. Потерянный IRQ не останавливает опрос панели.
Повтор целого пакета отсеивает общий парсер GUI; повреждённый префикс ресинхронизируется.

`controls_uart_metrics()` предоставляет пакеты, ошибки запуска/передачи и таймауты.
`Stm32Controls` хранит переполнения очереди и недопустимые переходы фаз.
Длительные нажатия вычисляет GUI по DOWN/UP, отдельной команды long press нет.

Порядок записи и проверки на STM32 — [FLASHING.md](FLASHING.md#stm32).
Данные MCU — [ST STM32F103RC](https://www.st.com/en/microcontrollers-microprocessors/stm32f103rc.html),
генерация — [STM32CubeMX CLI](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.0/en/docs/markup/CubeMX_CLI.html).
