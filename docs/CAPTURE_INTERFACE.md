# Интерфейс захвата, Linux ABI 3

## Raw samples и метаданные

Формат 1: unsigned 8-bit offset binary, два канала, в памяти `CH1[t], CH2[t], CH1[t+1], CH2[t+1]`. Для двух ветвей A/B такт входа packer содержит CH1_A, CH2_A, CH1_B, CH2_B; B следует за A во времени. Порядок проверен RTL-стендом; достоверность кодов и калибровка интерливинга на плате пока не измерены.

`CaptureBuffer` хранит pointer/capacity/bytes, sequence, sample_rate_hz **на канал**, first_sample, trigger_sample (абсолютные индексы), аппаратное и software время, flags, volts_per_code и zero_code на канал. Нет триггера: UINT64_MAX. `volts=(code-zero_code)×volts_per_code`. Calibration аппаратного source задаёт вызывающая сторона; численные значения demo не относятся к плате.

`DisplayFrame` содержит Y, min/max, FFT и измерения только для отображения. Raw запись имеет независимую глубину; при остановке она сохраняется под владением processor. Изменение view повторно обрабатывает исходные данные, не старые Y.

## Регистры PL и аппаратный блок

В текущем bitstream AXI DMA S2MM/SG находится по `0x40400000`, IRQ F2P[1]
(GIC SPI 30), capture AXI GPIO — по `0x41210000`. OLED сохраняет `0x41200000`.
GPIO.DATA offset 0, bit 0 включает поток; GPIO.DATA2 offset 8 содержит
`0xADC30000 | overflow`. DATA[5:1] выбирает частоту по `capture_rates.h`,
DATA[10:6] задаёт payload shift. Настройки меняются при выключенном потоке.
Такты АЦП от enable не зависят. Проверка ID обязательна
до запуска DMA. Register version=3, bitstream interface version=1;
идентичность конкретной сборки отдельно публикуется штатным SHA-256 bitstream.

Один AXIS пакет содержит 64 байта заголовка и `8 << payload_shift` байт
отсчётов (shift 0…17), TLAST на последнем 64-битном beat.
Первые 64 байта — little-endian заголовок:

| Offset | Содержимое |
|---:|---|
| 0 | uint32 magic `0x5043534F` (OSCP) |
| 4 | uint32 header version 3 |
| 8 | uint32 payload bytes |
| 12 | uint32 effective sample rate per channel |
| 16 | uint64 sequence |
| 24 | uint64 first_sample |
| 32…63 | Нули, резерв |

Затем выбранные пары `CH1[t],CH2[t],CH1[t+1],CH2[t+1],…`.
На 200 MSa/s используются A и B; на меньших частотах выбирается ветвь A
с равномерным целым периодом. Счётчики относятся к эффективным отсчётам,
сбрасываются при новом open и CONFIGURE;
входные преобразования и конвейер прогреваются до его включения.
`hardware_ns=first_sample*1e9/sample_rate_hz` — относительное время отсчётов,
не wall clock и не время IRQ. Несовпадение заголовка завершённого DMA-блока
возвращает ошибку. Подключение и расчёт задержек — [ADC_CAPTURE.md](ADC_CAPTURE.md).

## Linux UAPI

`capture_uapi.h` задаёт fixed-width structs с __aligned_u64, одинаковыми для ARMv7 и 64-bit userspace. GET_INFO возвращает ABI/register/sample/bitstream versions и геометрию DMA ring. mmap — только выделенная драйвером память, read-only для приложения. DEQUEUE после poll возвращает completed buffer index/bytes/data_offset/sequence/timestamps/trigger; RELEASE явно возвращает владение. Неверные размеры/индексы/версии отвергаются адаптером.

Software ceiling — 128 блоков. Реальный драйвер выделяет 16 блоков по 2 МиБ; GET_INFO.block_bytes — шаг между блоками, включая аппаратный заголовок. DEQUEUE.data_offset=64, bytes содержит только payload. Адаптер проверяет сумму offset+bytes, выравнивание, версии и флаги до доступа к отображению.

CONFIGURE принимает rate_index 0…19 и payload_shift 0…17. При существующем
lease возвращает EBUSY. Драйвер завершает текущий поток, сбрасывает очередь
и запускает новый с соответствующими длиной дескрипторов и GPIO-настройкой.
Приложение выбирает длительность блока не более 40 мс, максимум 1 МиБ payload.

Между DEQUEUE и RELEASE DMA не пишет в leased buffer. coherent allocation
обеспечивает согласование CPU/device; перед разбором завершения используется
dma_rmb. RELEASE возвращает блок в SG-очередь. При закрытии устройства DMA
останавливается; память удерживается до освобождения mmap references.
Приложение не получает физический адрес, mmap read-only, повышение до записи
через mprotect запрещено. Входное overflow и отсутствие завершений дают POLLERR;
пропуск аппаратного sequence даёт OSC_CAPTURE_DISCONTINUITY.

`ZYNQ7020/linux/driver/oscill_capture.c` встроен в ядро через Buildroot hook.
`capture_zynq.h/c` обслуживает mmap/poll/ioctl и leases. `capture_pipeline`
подключает его отдельным worker. `capture_history` копирует выбранную глубину
в кешируемую историю и сразу возвращает DMA lease. Разрыв first_sample или
смена частоты очищает накопленную историю. Отдельный снимок сохраняется для
STOP и ожидания NORMAL/SINGLE. Программная подмена
выполняется только после DEQUEUE; сведения о времени/sequence не генерируются.
Отсутствие устройства возвращает ошибку без перехода на демоданные.

## Файл OSCRAW1

`capture_file.h/c` — версия 1, header 128 байт, затем полный raw payload. Все целые little-endian, calibration IEEE-754 binary64. В заголовке: magic `OSCRAW1\0`; ABI/header length/channels/format; rate/first/sequence/trigger/hardware/software timestamps; payload length; два gain, два zero; flags. Смещения соответствуют serializer в исходниках. CRC32 IEEE отдельно для metadata bytes 0…115 и raw payload; поле metadata CRC offset 116, payload CRC 120, reserved 124.

Reader проверяет длину, версии, диапазоны, finite calibration, CRC и отсутствие лишних байтов. Большие captures не сериализуются через CSV или экранные координаты. Прежний BMP/CSV codec сохранён; он остаётся форматом совместимого экранного экспорта. Бинарный writer имеет FILE API, а не скрытые пути или временные файлы. UI фонового сохранения полного raw capture пока не подключён.
