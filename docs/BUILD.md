# Сборка

Проект ПЛИС открывается в Vivado 2019.1 файлом [`ZYNQ7020/oscill.xpr`](../ZYNQ7020/oscill.xpr); схема и необходимые IP входят в Git. Полная сборка Zynq запускается из корня репозитория командой `./ZYNQ7020/linux/build.ps1` после подготовки окружения. Результат находится в `ZYNQ7020/output/linux/`: `BOOT.BIN`, `image.bin`, `qspi.bin` и `fsbl.elf`.

GUI также собирается отдельно для Windows и Linux, панель STM32 — проектом Keil из `STM32/MDK-ARM/`. Подготовка окружения и команды отдельных этапов приведены в [инструкции разработчика](agents/BUILD.md).

Для обновления Zynq подключите USB–UART P1 и во вкладке «Прошивка» Oscill Terminal выберите каталог с `BOOT.BIN` и `image.bin`. Запись через SDK и прошивка STM32 описаны в [инструкции по записи](agents/FLASHING.md).
