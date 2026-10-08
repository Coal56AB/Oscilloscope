# Выполненные проверки

Проверки выполнены 8 октября 2026 на Windows, Intel Core i5-14600KF (14 cores / 20 logical processors). Доступа к работающей Zynq/STM32 и сенсорному контроллеру в сеансе нет. Результат ARM compilation не является проверкой на устройстве.

## Сборки и функциональные тесты

| Проверка | Результат |
|---|---|
| Корневой CMake, Release, llvm-mingw 20240619 / Clang 18.1.8 | Windows WinAPI frontend и все переносимые targets собраны |
| CTest Windows | 4/4: `scope_simulator`, `scope_runtime`, `stm32_controls`, `stm32_controls_uart` |
| CubeMX 6.12.1 / STM32CubeF1 1.8.7 | Проект F103RCT6 сгенерирован; повторная генерация сохраняет USER CODE, прикладные файлы, группы/пути Keil, compiler и output |
| `python STM32/tests/check_project.py` | PASS: MCU, SWD, UART, clock, точки подключения приложения, исходники/include Keil и каталог результатов |
| Keil MDK 5.38 / Arm Compiler 6.19 | Все C-модули компилируются без предупреждений; assembler не запускается из-за `A9555E / R207(3): REGISTRY READ ERROR`, итогового Keil HEX/AXF нет |
| GNU Arm 15.2.1, Cortex-M3, `-Wall -Wextra -Werror` | Все C-файлы из Keil-проекта скомпилированы и слинкованы с официальным GCC startup F103xE; ELF/HEX созданы отдельно для проверки, это не результат линковки Keil |
| ARM Linux GCC 8.2.0, Cortex-A9 / NEON / hard-float | SDL frontend, runtime, Zynq adapter и evdev собраны и слинкованы в вариантах DRM и framebuffer; framebuffer собирается без libdrm |
| Target SDL2 2.30.12 | ARM static library собрана из исходников |
| Target libdrm 2.4.124, Meson 1.7.2 | ARM static library собрана из исходников |
| `readelf -h -A scope_preview` | ELF32 ARM little-endian, EABI5 hard-float, ARMv7-A, VFPv3, NEON |
| DRM/framebuffer/evdev translation units, ARM `-Wall -Wextra -Werror` | Скомпилированы |
| Vivado 2019.1, полный PS/VDMA/VTC/TMDS проект | Синтез, размещение и разводка завершены; WNS 1,298 нс, WHS 0,029 нс, timing проходит |
| SDK 2019.1, FSBL и диагностическое приложение | Собраны из полученного handoff; `BOOT.BIN` содержит FSBL, bitstream и приложение |
| U-Boot 2024.10, Bootlin GCC 13.3.0 | Собран `u-boot.elf` с Device Tree платы; запуск в QEMU 10.2.0 Zynq: 496 МиБ RAM, UART1, два SD-контроллера, адреса скрипта, два чтения и сравнение 16 МиБ эмулированной QSPI |
| ELF/BOOT layout | FSBL расположен в OCM, диагностическое приложение ниже 32 МиБ; magic и checksum Zynq boot header проверены, диапазоны разделов укладываются в файл |
| Vivado 2019.1, `axis_capture_pack_tb` | PASS: 32 AXI beats, порядок, TLAST, удержание при backpressure |
| `python ZYNQ7020/linux/tests/test_qspi.py` | 3/3: exact fit, превышение границы на байт, A/B budget, пустой image |
| `bash -n` | Linux build/start/card scripts и сохранённый Tinker build script проходят syntax check |
| `git bundle verify` | Полная сохранённая история GUI, bundle исправен |
| PCB working tree | Чистое |
| Документация и миграция | Оба исходных ТЗ сохранены целиком; исходные GUI файлы и PNG сохранены; локальные ссылки разрешаются |
| Buildroot 2025.02.12 / Bootlin GCC 13.3.0 | Полная сборка U-Boot, Linux 6.6.70, DTB, SDL2, ARM GUI и SquashFS завершена на Debian build host |
| QEMU 10.2.0, загрузка с SD и QSPI | U-Boot исполняет штатные скрипты, Linux монтирует SquashFS, создаёт framebuffer 1024×600 и автоматически запускает GUI; QSPI запуск выполнен без SD-устройства |
| ARM GUI в Linux | Процесс работает с `--fb /dev/fb0 --touch auto`; кадр прочитан из framebuffer и визуально проверен |
| `tests/verify_release.py` и чтение FAT32 через mtools | PASS: Zynq header/checksum, CRC скриптов, MBR/FAT32, все загрузочные файлы, rootfs, payloads и свободные области QSPI |

`scope_runtime` проверяет CRC16 known vector, фрагментацию/ресинхронизацию UART, duplicate/sequence wrap/gaps, queue overflow, правила владения raw buffers, все три full policies, сохранение узкого импульса min/max при уменьшении до экранных столбцов, raw read/write и CRC corruption, threaded acquisition/processing, STOP и смену масштаба после STOP. STM32 тест проверяет дребезг/переполнение timestamps, DOWN/UP, quadrature cycle, невалидный переход и TX overflow. Assertions в новых тестах действуют и в Release.

В timing report нет внутренних endpoints без ограничений или registers без clock. Внешние задержки приёмника HDMI не заданы; сигнал на разъёме ещё не измерен. В сгенерированном VDMA включён переход между AXI 100 МГц и AXIS 50 МГц. Диагностический тест DDR использует отдельные адресные биты 2…28, walking data и два полных прохода 464 МиБ; перед чтением результатов выполняется ARM `dsb`.

`stm32_controls_uart` проверяет HAL busy, неизменность активного буфера,
подтверждение только после TX complete, повтор ошибки с тем же пакетом,
потерянный IRQ, переполнение tick и callback во время запуска HAL.
ARM-проверка STM32 использует Flash 256 КиБ / SRAM 48 КиБ; аппаратных результатов
для панели, частоты HSI и USART пока нет.

## Сохранение изображения исходного GUI

Исходный GUI `16c10c6` и текущий WinAPI frontend собраны одним Clang toolchain. BMP получены отдельными тестовыми запусками `--snapshot` и `--panel-snapshot`; чужие работающие окна не закрывались. Пары совпадают побайтно по SHA-256:

- LCD: `C9DD4111EE8C02CF1F5B72BF9478FF0D0048E6812CC76F888C2B7A6824207B67`.
- Panel: `354536A0154A7EF9EB1BE7CC8C248545011C8998F08C584F7C93BB4457C65C5B`.

Это проверка начального состояния и отрисовки, а не всех интерактивных последовательностей. Исходный simulator regression suite также проходит.

## Программная нагрузка на ПК

Release `scope_benchmark RATE 3`: отдельные producer/processor/render потоки, 8 × 1 МиБ, producer копирует заранее подготовленный шаблон, processor выполняет raw statistics/min-max/FFT, renderer рисует CPU кадр. Память повторно используется и может находиться в cache. Здесь нет PL, DMA, USB или HDMI scanout. Частота захвата в metadata не является фактически выполненным ADC sampling.

| Цель, МБ/с | Опубликовано, МБ/с | Обработано, МБ/с | Блоки обработано/всего | Overwrite | Drop | Peak | FPS | Среднее processing, мс | CPU, % одного ядра |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 100,31 | 99,95 | 287/287 | 0 | 0 | 1 | 60,00 | 2,887 | 25,9 |
| 200 | 200,28 | 199,70 | 573/573 | 0 | 0 | 1 | 59,67 | 2,724 | 40,5 |
| 400 | 400,21 | 398,35 | 1145/1145 | 0 | 0 | 2 | 59,67 | 2,539 | 95,9 |
| 500 | 499,82 | 412,15 | 1186/1430 | 244 | 0 | 7 | 60,00 | 2,541 | 99,9 |

Средняя отрисовка: 0,461–0,509 мс; максимальная: 0,785–1,052 мс. Максимальный промежуток между началами кадров: 17,588–17,836 мс. Нагрузка 500 МБ/с превышает возможности данного processor на этом ПК; overwrite ожидающих READY учитывается, CPU-owned блоки защищены. Processed rate включает время drain после завершения producer, published rate делится на заданные 3 с. FPS относится к вызовам CPU renderer, не к предъявлению HDMI кадров.

`corrupt=0` во всех четырёх прогонах — выборочная проверка байтов через 4096 байт, а не CRC всего потока. Полный CRC payload проверяется отдельно в файловом тесте. Для измерений DDR/DMA нужен аппаратный PL test pattern с sequence и проверкой полного payload.

## Невыполненные аппаратные и системные проверки

FSBL, HDMI bitstream и полный Linux-комплект собраны. GUI запущен в ARM Linux в QEMU с SD и QSPI. Эмулятор запускает U-Boot ELF напрямую на одном CPU: BootROM/FSBL/PL, реальная DDR, второй CPU, HDMI-разъём и USB3320/HID сенсора этим прогоном не проверяются. USB PHY в модели не отвечает; это не результат проверки USB платы. Tinker ARM32/ARM64 package не пересобирался; source list и shell syntax обновлены.

На устройстве предстоит проверить:

1. DDR handoff, UART console, восстановление и загрузку QSPI без карты.
2. HDMI mode/pixel clock и длительную работу около 60 Hz. Первый Linux-комплект копирует кадры в одну framebuffer-поверхность; page flip относится к отдельному DRM backend.
3. USB Host/VBUS, фактический HID multitouch минимум с двумя контактами, pinch и отсутствие mouse duplicates; reconnect/SYN_DROPPED.
4. Физический UART STM32, последовательности encoder/down/up/hold/chord, повреждения и разрыв связи.
5. DMA PL pattern сначала без видео, затем с HDMI и обработкой: 100/200/400 МБ/с, повышенная нагрузка и soak. Полный контроль payload, FIFO/SG/queue counters и cache coherency.
6. STOP/pretrigger/posttrigger, масштабирование всей сохранённой raw записи, реальную задержку реакции интерфейса.
7. Raw export, SD speed/full/eject/reinsert и сохранность файлов.

До этих проверок нет подтверждения 400 МБ/с на Zynq с HDMI и 20–30% запасом. Расчёт потенциального DDR bottleneck — [PERFORMANCE.md](PERFORMANCE.md), оставшаяся реализация — [DECISIONS.md](DECISIONS.md).
