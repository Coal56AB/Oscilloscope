# Интерфейс захвата, версия 1

## Raw samples и метаданные

Формат 1: unsigned 8-bit offset binary, два канала, в памяти `CH1[t], CH2[t], CH1[t+1], CH2[t+1]`. Для двух ветвей A/B такт входа packer содержит CH1_A, CH2_A, CH1_B, CH2_B; B следует за A во времени. Перестановка и калибровка интерливинга должны быть проверены до включения ADC.

`CaptureBuffer` хранит pointer/capacity/bytes, sequence, sample_rate_hz **на канал**, first_sample, trigger_sample (абсолютные индексы), аппаратное и software время, flags, volts_per_code и zero_code на канал. Нет триггера: UINT64_MAX. `volts=(code-zero_code)×volts_per_code`. Calibration аппаратного source задаёт вызывающая сторона; численные значения demo не относятся к плате.

`DisplayFrame` содержит Y, min/max, FFT и измерения только для отображения. Raw запись имеет независимую глубину; при остановке она сохраняется под владением processor. Изменение view повторно обрабатывает исходные данные, не старые Y.

## Предлагаемая карта PL-регистров

Это контракт будущего peripheral, не адреса существующего bitstream. AXI base/IRQ/DMA phandles назначаются реальным Vivado design.

| Offset | Регистр | Значение |
|---:|---|---|
| 00 | ID | ASCII OSCP, read-only |
| 04 | Register version | 1 |
| 08 | Sample format | 1 |
| 0C | Bitstream build | Уникальная идентичность сборки |
| 10 | Control | ARM/DISARM/RESET counters |
| 14 | Status | Armed/triggered/stopped/FIFO overflow/sync error |
| 18 | Channel mask | Два активных канала |
| 1C | Trigger config | Source/edge/mode |
| 20 | Trigger level | Raw code |
| 24 | Pretrigger samples | Глубина на канал |
| 28 | Posttrigger samples | Глубина на канал |
| 2C | Holdoff samples | Интервал между триггерами |
| 30/34 | Sample counter | 64-bit, атомарный snapshot |
| 38/3C | Trigger sample | 64-bit, атомарный snapshot |
| 40 | FIFO overflow count | Saturating/reset explicitly |
| 44 | Sync error count | Saturating/reset explicitly |
| 48 | FIFO high water | Для измерения stalls |

Многоразрядные значения должны читаться через snapshot/latch или согласованный high/low/high алгоритм. Предварительное выделение offsets не резервирует реальные ресурсы платы.

## Linux UAPI

`capture_uapi.h` задаёт fixed-width structs с __aligned_u64, одинаковыми для ARMv7 и 64-bit userspace. GET_INFO возвращает ABI/register/sample/bitstream versions и геометрию DMA ring. mmap — только выделенная драйвером память, read-only для приложения. DEQUEUE после poll возвращает completed buffer index/bytes/sequence/timestamps/trigger; RELEASE явно возвращает владение. Неверные размеры/индексы/версии отвергаются адаптером.

Software ceiling — 128 блоков; это допускает 256 МиБ при блоках 2 МиБ и не создаёт автоматической резервации RAM. Драйвер возвращает фактическую доступную геометрию, а размер hardware SG descriptor согласуется с TLAST.

Между DEQUEUE и RELEASE DMA не пишет в leased buffer. Драйвер публикует completion только после необходимого cache sync; после RELEASE — sync for device. При закрытии fd освобождает все leases. mmap mapping должен оставаться действительным до release последнего reference; driver не передаёт raw physical address в userspace. Ошибки DMA/overflow отражаются в status и dropped-block counters, запись с discontinuity не объявляется непрерывной.

`capture_zynq.h/c` реализует userspace стороны mmap/poll/ioctl и lease; сейчас в репозитории **нет** kernel driver с этим ABI и этот adapter **ещё не подключён** к demo pipeline. Он собирается по `SCOPE_ENABLE_ZYNQ=ON`, но не создаёт искусственный `/dev` source. Для интеграции нужны реальный dmaengine driver, его Device Tree и общий provider интерфейс leases; memcpy полного raw потока в demo heap запрещён.

## Файл OSCRAW1

`capture_file.h/c` — версия 1, header 128 байт, затем полный raw payload. Все целые little-endian, calibration IEEE-754 binary64. В заголовке: magic `OSCRAW1\0`; ABI/header length/channels/format; rate/first/sequence/trigger/hardware/software timestamps; payload length; два gain, два zero; flags. Смещения соответствуют serializer в исходниках. CRC32 IEEE отдельно для metadata bytes 0…115 и raw payload; поле metadata CRC offset 116, payload CRC 120, reserved 124.

Reader проверяет длину, версии, диапазоны, finite calibration, CRC и отсутствие лишних байтов. Большие captures не сериализуются через CSV или экранные координаты. Прежний BMP/CSV codec сохранён; он остаётся форматом совместимого экранного экспорта. Бинарный writer имеет FILE API, а не скрытые пути или временные файлы. UI фонового сохранения полного raw capture пока не подключён.
