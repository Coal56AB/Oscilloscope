# Управление STM32 → Zynq, версия 1

Канонический кодек — `ZYNQ7020/gui/src/control_protocol.h/c`; STM32 использует ту же C-библиотеку. UART: 8N1, без flow control, исходное значение 115200 baud; GUI принимает `--serial DEVICE --baud RATE`. Linux adapter поддерживает 9600/19200/38400/57600/115200/230400/460800/921600, если константы доступны в termios. Менять параметры можно без прошивки Zynq.

## Пакет

20 байт, little-endian, без C struct serialization.

| Смещение | Размер | Поле |
|---:|---:|---|
| 0 | 2 | Magic A5 5A |
| 2 | 1 | Version = 1 |
| 3 | 1 | ROTATE=1, DOWN=2, UP=3, HEARTBEAT=4 |
| 4 | 1 | Полная длина = 20 |
| 5 | 1 | Control ID |
| 6 | 2 | Sequence uint16, modulo 65536 |
| 8 | 4 | Uptime timestamp_ms uint32, modulo 2³² |
| 12 | 4 | Delta int32 для ROTATE, 0 для других |
| 16 | 2 | Reserved = 0 |
| 18 | 2 | CRC16-CCITT-FALSE по байтам 2…17: poly 1021, init FFFF, no reflection/xorout |

IDs 0…4: CH1, CH2, TIME, TRIGGER, FUNCTION encoders. 5…10: CH1, CH2, CURSOR, MODE, MENU, RUN buttons. HEARTBEAT: ID FF, value 0. ROTATE допустим только для encoder, delta −4096…4096 кроме 0; большие накопленные серии разбиваются на несколько пакетов. DOWN/UP применимы также к кнопке энкодера.

CRC test vector `123456789` → 29B1. Парсер принимает фрагменты по одному байту, отбрасывает некорректную версию/длину/reserved/id/CRC и ищет следующее magic внутри повреждённого пакета. Никакая команда из невалидного пакета не применяется.

## Потери и переподключение

Sequence присваивается на стороне STM32 при формировании события, включая потерянное из-за полного TX ring. Повтор/старый sequence отвергается; разница 1…32767 движется вперёд, разница 0 или ≥32768 — duplicate/stale. При новой связи парсер начинает новую эпоху. STM32 reboot без изменения открытого UART обнаруживается после двухсекундного отсутствия принимаемых пакетов и переоткрытия.

Heartbeat формируется раз в секунду. Serial worker не блокирует GUI; Linux использует O_NONBLOCK/poll, Windows COM timeouts. При I/O error или >2 с без валидного пакета соединение закрывается, повторное открытие не чаще 500 мс. Доступность устройства не выдаётся за установленную связь со STM32.

`serial_control_metrics()` возвращает согласованный snapshot: принятые пакеты, CRC/format errors, sequence gaps/duplicates, I/O errors и открытия порта. Счётчики накопительные за время работы transport, переживают reconnect. `connected` выставляется после первого валидного пакета и сбрасывается при timeout/I/O error. SDL `--diagnostics` выводит эти счётчики; прямое чтение worker-owned parser из GUI не требуется.

Очередь GUI 256 событий. При переполнении pending events отбрасываются со счётчиком и generation reset; незавершённые удержания отменяются. Sequence gap также отменяет незавершённые удержания: потерянный UP не превращается в вечное удержание. Это обнаружение потери, а не обещание доставки. При переполнении увеличивать baud/частоту обслуживания или размер очереди после измерений.

В SDL frontend DOWN/UP клавиатуры и UART проходят через одну очередь, ROTATE вызывает `demo_signal_rotate`, нажатия — прежний механизм `demo_signal_press_at`. Длительность вычисляется по source timestamps для пакетов, пришедших одной серией, и по host monotonic clock для текущего удержания. После длинного нажатия короткое не повторяется. CH1+MENU сохраняет прежнюю комбинацию; точная синхронизация такой комбинации при большой UART latency требует проверки на устройстве.

## Переносимая сторона STM32

`STM32/Core/Inc/controls.h` и `STM32/Core/Src/controls.c` принимают нормализованные GPIO состояния, а не номера pins. Quadrature decoder учитывает последовательные переходы и invalid jumps; configurable 1…4 перехода/детент. Buttons используют configurable debounce и сохраняют timestamp начала устойчивого перехода, включая wrap uptime. TX ring: 64 события. UART adapter берёт packet через `peek` и удаляет через `sent` после TX complete; при ошибке повторяет событие с тем же sequence.

CubeMX/HAL проект STM32F103RCT6 читает входы по Interfaces.SchDoc и передаёт пакеты через USART1 в режиме IT. Настройка и таблица GPIO — [STM32.md](STM32.md). Формирование событий, codec и асинхронный UART проверяются без платы; физическое управление остаётся аппаратной проверкой.
