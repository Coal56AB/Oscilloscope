# Архитектура осциллографа

## Компоненты

`ZYNQ7020/` содержит одно устройство: приложение, PL и Linux. `STM32/` содержит контроллер органов управления. `PCB/` остаётся независимым Altium-субмодулем. Полные требования сохранены в [REQUIREMENTS.md](REQUIREMENTS.md), подтверждённое железо — в [BOARD.md](zynq7020/BOARD.md).

```text
AD9288 ×2 → capture/CDC → A/B ordering → FIFO → AXI DMA → DDR raw ring
                              └→ min/max/statistics → display processing
                                                        ↓
USB touchscreen → Linux evdev → SDL events → GUI state → ScopeScreen → CPU framebuffer
STM32 encoders/buttons → UART → ControlTransport ────────┘                  ↓
                                                             framebuffer → VDMA → PL TMDS → HDMI
DDR frozen record → storage worker → removable microSD
```

Захват, обработка и GUI имеют разные частоты. 200 MSPS/канал — частота raw; 400 МБ/с — сумма двух каналов; 60 FPS — цель обновления изображения, а не частота получения всех выборок.

## Переносимое приложение

- `capture.h/c`: raw `CaptureBuffer`, метаданные и кольцо владения; caller supplies storage.
- `capture_processor.h/c`: коды → вольты → статистика, min/max, 1024 экранных столбца и FFT; без вызовов оконной системы.
- `capture_pipeline.h/c`: acquisition и processing workers, удержание последней raw записи и передача `DisplayFrame` через короткую блокировку. Исходные данные при STOP не превращаются в Y-координаты; смена масштаба вызывает повторную обработку.
- `capture_file.h/c`: бинарная запись с метаданными и CRC; `FileCaptureSource` отдаёт проверенную полную запись. Заголовок читается при открытии, большой payload — в acquisition worker.
- `capture_adapter.h/c`: связывает подготовленный кадр с существующим `DemoSignal`/`ScopeScreen`. Предметный processor не зависит от этого адаптера.
- `control_protocol.h/c`, `control_transport.h/c`, `control_serial.c`: общий кодек, ограниченная очередь событий, keyboard/serial producers и отдельный UART worker.
- `STM32/Core/Oscill/panel_state.c`: владелец масштабов CH1/CH2 и выбора реле. GUI получает абсолютный масштаб, а сенсорный экран отправляет запрос изменения. GPIO/ADC отделены в `board_outputs.c`; режим позиции каналов и RUN синхронизируются обратным UART-каналом.
- `linux_display.c`, `linux_touch.c`: Linux framebuffer/DRM и evdev adapters; отключены по умолчанию в настольной сборке.

Демонстрационный настольный режим по умолчанию сохранён. SDL frontend дополнительно позволяет `--raw-demo` и `--capture file.oscraw`; новые raw источники не подменяют существующие BMP/CSV. Windows frontend сохраняет WinAPI и прежние операции; переносимые источники и кольцо собираются и тестируются на Windows отдельно от frontend.

Кольцо: FREE → FILLING → READY → READING → FREE. Метаданные и переходы защищены mutex; producer заполняет только принадлежащий ему FILLING, processor читает только READING. Короткая блокировка не удерживается во время копирования raw или обработки. Overwrite допускается только для READY. Stop/drop-new/overwrite-ready имеют отдельное поведение и счётчики. Один программный захват в файле ограничен 256 МиБ при загрузке demo pipeline; ограничение защищает бюджет RAM и не задаёт предел аппаратного DMA ABI.

Синтетический pipeline использует 8 × 2 МиБ heap-буферов. Это память программного источника, **не DMA allocator**. Hardware source получает `mmap` DMA memory от драйвера и явно возвращает lease; его нельзя заменить вызовом `malloc`.

## Linux и вывод

Выбран Buildroot с br2-external: один CMake-пакет приложения, rootfs-overlay, kernel/UBoot fragments и согласованный Device Tree. PS/DDR handoff и PL design создаются одним Vivado Tcl проектом; FSBL из этого handoff настраивает DDR и запускает видео перед передачей управления диагностике или U-Boot.

Программный рендерер остаётся CPU RGB888 в 32-битном слове `0x00RRGGBB`. Первый Linux-комплект использует `simple-framebuffer`: отображает `/dev/fb0` через kernel driver и копирует строки с учётом pitch. Одна поверхность допускает tearing; это первый аппаратный запуск без изменения DMA-регистров из GUI. Scanout начинается с `0x1f000000`, верхние 16 МиБ исключены из обычной Linux/U-Boot RAM.

Отдельный DRM backend использует XRGB8888 dumb buffer, две поверхности и page flip. Пока flip pending, кадр не копируется в занятый scanout-buffer. Он требует готового DRM driver с режимом 1024×600 около 60 Hz; такого драйвера для текущего PL ещё нет.

SDL2 KMSDRM требует GBM/EGL/[OpenGL либо OpenGLES](https://github.com/buildroot/buildroot/blob/2025.02.12/package/sdl2/Config.in). Прямые framebuffer/DRM backends используют SDL dummy video для общего event loop. X11/Wayland и Mesa не введены. Linux evdev Type B multitouch преобразуется в те же SDL_FINGER события; число контактов проверяется через ioctl, mouse duplicates отключены. `--touch auto` ищет устройство с двумя и более контактами. После SYN_DROPPED контакты перечитываются. Нормализация использует ABS limits, есть поворот 0/90/180/270; сложная матричная калибровка не реализована.

PL video: AXI VDMA MM2S через HP2 → 32/24-bit AXIS converter → video out/VTC → Digilent rgb2dvi/OSERDES → подтверждённые HDMI pins. FCLK0 — 100 МГц, pixel clock — 50 МГц, serializer clock — 250 МГц DDR, totals — 1344×620, active — 1024×600. Получается около 60,005 Гц. XDC и проект проходят timing для `xc7z020clg400-1`. На схеме нет DDC/HPD; совместимость этого фиксированного режима с экраном проверяется на устройстве.

## Служебный OLED

Задний OLED обслуживает отдельная Linux-служба `oscill-status`: модель страниц,
опрос sysfs, неблокирующая телеметрия GUI и UIO-драйвер GPIO разделены.
Переносимый OLED renderer/SSD1306 transport используется также FSBL. K1
переключает GUI, acquisition/processing, связь, питание, накопитель, систему,
ошибки и все температурные страницы. Подробности — [STATUS_DISPLAY.md](STATUS_DISPLAY.md).

## DMA и триггер

В STOP временной масштаб меняет вид сохранённой записи. `waveform_view`
сохраняет запрошенный интервал, за пределами памяти возвращает пропуски, которые
renderer не соединяет линией. Raw processor считает статистику только по
пересечению с памятью; отображение более длинного интервала не запускает захват.
Генератор и CSV используют тот же механизм. Форматы BMP/CSV сохранены.

`capture_uapi.h` задаёт Linux ABI 4; описание — [CAPTURE_INTERFACE.md](CAPTURE_INTERFACE.md).
Встроенный драйвер `linux/driver/oscill_capture.c` выделяет coherent DMA-память,
проверяет аппаратный ID и заголовки, обслуживает SG и владение блоками.
`capture_zynq.c` отображает только эту память read-only. Zynq-7000 HP не
предоставляет автоматической coherency с ARM: здесь используется coherent
allocation с dma_rmb перед чтением завершения, а не streaming mapping.

FPGA принимает обе ветви двух AD9288, прореживает отсчёты с выбранным шагом
и записывает пакеты через HP0; HDMI продолжает использовать HP2. GUI сразу
возвращает DMA-блок после копирования в историю. STOP удерживает программный
снимок истории, поэтому масштабирование не занимает DMA-память.
DEBUG заменяет содержимое отдельной программной записи после настоящего
захвата, сохраняя аппаратные sequence и время. Общий `signal_generator`
используют настольный GUI и подмена, а драйвер о ней не знает.

AUTO/NORMAL/SINGLE, уровень, фронт, holdoff и FORCE обрабатываются программно
на полученных блоках. Аппаратного кольцевого pretrigger на всю память пока нет.
Геометрия, режимы и границы аппаратной проверки — [ADC_CAPTURE.md](ADC_CAPTURE.md).

## Хранилище и загрузка

Постоянная система хранит FSBL, boot PL, U-Boot, kernel/DTB и SquashFS с приложением в 16 МиБ QSPI. Собран единый `qspi.bin`; U-Boot выбирает скрипт по boot mode. Для первого запуска подготовлен также `sdcard.img`: `BOOT.BIN`, `boot.scr`, kernel и DTB в FAT32, rootfs во втором разделе карты TF1. Оба Linux-комплекта используют тот же handoff, что и тест DDR/HDMI. Сборщик QSPI рассчитывает разделы и проверяет вместимость по фактическим файлам. Старт Linux/GUI с SD и QSPI проверен в эмуляторе. [FLASHING.md](FLASHING.md) описывает запись и восстановление.

TF2 предполагается съёмным хранилищем exFAT. `oscill-card` явно монтирует выбранное block device и выполняет sync/unmount. Отсутствие карты не блокирует старт GUI, `--data-dir` направляет BMP/CSV к выбранному каталогу; ошибку записи приложение показывает. Бинарный raw codec готов, но UI фонового экспорта больших записей и автоматическое определение карты пока не подключены. Синхронный BMP/CSV browser существующего frontend сохранён; перенос этих операций в storage worker остаётся отдельной работой.

## Версии

Контракт захвата: карта регистров — 5, driver ABI — 4, sample format — 2. UART protocol — 1, app version — 0.1.0. Аппаратный ID захвата — `0xADC50000` без флагов ошибок. Manifest фиксирует Git SHA исходников, toolchain/Buildroot/kernel/U-Boot и хеши boot/bitstream/kernel/DTB/rootfs. Userspace capture adapter отвергает неверную ABI версию до доступа к payload. Миграция исходного GUI и сохранение истории описаны в [REPOSITORY.md](REPOSITORY.md).
