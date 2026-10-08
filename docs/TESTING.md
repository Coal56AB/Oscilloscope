# Выполненные проверки

Проверки выполнены 8 октября 2026 на Windows, Intel Core i5-14600KF (14 cores / 20 logical processors). Доступа к работающей Zynq/STM32 и сенсорному контроллеру в сеансе нет. Результат ARM compilation не является проверкой на устройстве.

## Сборки и функциональные тесты

| Проверка | Результат |
|---|---|
| Корневой CMake, Release, llvm-mingw 20240619 / Clang 18.1.8 | Windows WinAPI frontend и все переносимые targets собраны |
| CTest Windows | 4/4: `scope_simulator`, `scope_runtime`, `stm32_controls`, `stm32_controls_uart` |
| Windows MSVC 19.29 / VS 2019, текущая интеграция Linux | Release собран; CTest 4/4 |
| Linux GCC 11.4 / SDL2 2.0.20, framebuffer backend | Release собран; CTest 5/5, включая `scope_linux_touch` |
| Evdev с подменой системных вызовов | Single touch, один и два MT слота, поворот, чтение нескольких пакетов за кадр, SYN_DROPPED, отпускание при disconnect и восстановление активного контакта при reconnect; pointer-устройство отклоняется |
| Linux display/touch `-Wall -Wextra -Werror` | Без предупреждений |
| Раздельные каталоги результатов | GUI Windows собран в `ZYNQ7020/gui/output/windows`, CTest 2/2; STM32 host-тесты собраны в `STM32/output/tests/windows`, CTest 2/2; Keil output проверен скриптом проекта; Linux BOOT после переноса совпадает по SHA-256 |
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

`scope_runtime` проверяет CRC16 known vector, фрагментацию/ресинхронизацию UART, duplicate/sequence wrap/gaps, queue overflow, правила владения raw buffers, все три full policies, сохранение узкого импульса min/max при уменьшении до экранных столбцов, raw read/write и CRC corruption, threaded acquisition/processing, STOP и смену масштаба после STOP. STM32 тест проверяет приём DOWN/UP и шагов энкодера, переполнение timestamps и TX overflow. Assertions в новых тестах действуют и в Release.

В timing report нет внутренних endpoints без ограничений или registers без clock. Внешние задержки приёмника HDMI не заданы; сигнал на разъёме ещё не измерен. В текущей сборке VDMA MM2S работает на 100 МГц, отдельный AXI clock converter передаёт поток в видео 50 МГц. Сборка `ZYNQ7020/oscill.xpr` после этого изменения прошла: WNS 1,307 нс, WHS 0,020 нс; SDK собрал FSBL и диагностический BOOT из нового HDF. Диагностический тест DDR использует отдельные адресные биты 2…28, walking data и два полных прохода 464 МиБ; перед чтением результатов выполняется ARM `dsb`.

Комплект с экранным журналом загрузки проверен в QEMU 6.2: U-Boot ELF читает FIT из полного QSPI-образа без SD, проверяет три SHA-256, Linux распаковывает initramfs и автоматически запускает штатные демосигналы с UART diagnostics. Для SD исполнен штатный `boot.scr` с корнем SquashFS. В обоих случаях активная консоль GUI — tty2; `S99oscill stop` завершает процесс и возвращает tty1. Кадры 1024×600 прочитаны из памяти framebuffer; визуально проверены сообщения ядра/rcS перед запуском GUI и сам QSPI GUI с двумя демосигналами. BootROM, FSBL, PL и USB-сенсор этот прогон не проверяет.

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
3. USB Host/VBUS, фактический HID single touch или multitouch, pinch при наличии двух контактов и отсутствие mouse duplicates; reconnect/SYN_DROPPED.
4. Физический UART STM32, последовательности encoder/down/up/hold/chord, повреждения и разрыв связи.
5. DMA PL pattern сначала без видео, затем с HDMI и обработкой: 100/200/400 МБ/с, повышенная нагрузка и soak. Полный контроль payload, FIFO/SG/queue counters и cache coherency.
6. STOP/pretrigger/posttrigger, масштабирование всей сохранённой raw записи, реальную задержку реакции интерфейса.
7. Raw export, SD speed/full/eject/reinsert и сохранность файлов.

До этих проверок нет подтверждения 400 МБ/с на Zynq с HDMI и 20–30% запасом. Расчёт потенциального DDR bottleneck — [PERFORMANCE.md](PERFORMANCE.md), оставшаяся реализация — [DECISIONS.md](DECISIONS.md).

## Скругление и заставка

`scope_rendering` проверяет сохранение фона снаружи углов, симметрию верхних и нижних углов, промежуточное покрытие пикселей, сохранение светлой рамки, выход виджета за границы кадра и рендер меню/карточек/просмотра файлов с фиксированными радиусами 5/10 px во всех режимах курсоров. Дополнительно проверяются симметрия BOTH, половинная ширина и центрирование RISING/FALLING, полная заливка RUN/STOP/BACK, отрисовка меню поверх нижних карточек, подписи оси FFT с графиком под ними, общая рамка таблицы курсоров, единый цвет CH1/CH2, диапазон и параметры шкалы FFT, положение подписи курсора FFT у обоих краёв, полная длина линий и горизонтальный режим dB, сохранение подписи TIME/VERTICAL при выборе A/B/FFT во всех шрифтах и обеих компоновках карточек и строчная `n` в обоих шрифтах. В `scope_simulator` проверяются диапазон 500 ns–1 s, масштаб старого CSV, две строки курсоров CH1/CH2, пристыковка Measurements справа от курсоров и подъём при расширении, центрирование меню после повторного открытия, четыре цифры курсоров на масштабах ns/us/ms и полный диапазон периодических демо-сигналов на медленной развёртке, частоты генератора до 1 MHz, спектральный пик синуса 1 MHz и отметка ALIAS при недостаточной частоте дискретизации, позиция и показания курсора FFT по логарифмической шкале при смене диапазона и выходе за границы, цикл FUNC A/B/FFT, автоматическое появление курсора FFT при включении FFT и курсоров в любом порядке, скрытие при OFF и отсутствие активации касанием или FUNC при OFF, FINE и уровни −80…0 dB. `scope_runtime` дополнительно проверяет одиночные импульсы в начале, конце и на границах интервалов при сжатии 4093 исходных отсчётов в 1024 столбца, полное покрытие положительных частот FFT и сохранение пиков между отображаемыми позициями вплоть до частоты Найквиста.

Для текущих исходников выполнены Windows Release (3/3 CTest) и Linux Debug с framebuffer/Zynq adapters (5/5, включая touch и SDL frontend). Windows-снимки текущего интерфейса находятся в `ZYNQ7020/gui/output/validation/interface/`, заставки — в `ZYNQ7020/gui/output/validation/rounding/splash.png`. Проверки simulator, rendering и runtime выполняют assert также в Release. Синтаксис GUI и видеозапуска проверен ARM Linux и bare-metal компиляторами. После просмотра Windows-версии владельцем пересобраны FSBL с логотипом, ARM GUI и Linux-образы; проверены заголовки, содержимое SD/QSPI, лимиты разделов, manifest SHA-256 и CRC архива. В доступном QEMU 6.2 прямая загрузка текущего ядра/initramfs доходит до login; framebuffer-модель не готова для запуска GUI, а параметр boot-mode для полной проверки QSPI этой версией не поддерживается. Сохранение заставки во время реальной загрузки, управление на плате и время старта пока не проверены.

`scope_linux_frontend` проверяет сохранение min/max на 10 ms/div в реальном `draw_all()` SDL frontend, восстановление привязки после raw frame, отказ экспорта BMP/CSV в tmpfs с сообщением `NO DATA CARD`, а также обычное сохранение и чтение файлов в настольном режиме. Ранний FSBL hook и работа D-cache проверены компиляцией и порядком вызовов в ELF; выигрыш времени и обнаружение HDMI требуют измерения на плате.

## Масштабирование исходного сигнала

`scope_runtime` проверяет сжатие одиночных импульсов и повторное построение увеличенного участка из того же raw-буфера, включая интерполяцию между исходными отсчётами и неизменность записи. `scope_simulator` проверяет независимую от обзора сетку демо-шума на 500 ns/div и повторяемость STOP при смене масштаба. `scope_rendering` проверяет цветной штрих толщиной 1 px при обычной развёртке и разных высотах окна ZOOM. Эти проверки выполняются на ПК; реальный захват АЦП ими не подтверждается.
