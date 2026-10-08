# Сборка разработчика

Команды ниже выполняются из корня репозитория. Выходные каталоги можно вынести за его пределы. Версии библиотек в выполненных проверках: SDL2 2.30.12, libdrm 2.4.124; Buildroot закреплён на 2025.02.12. Результаты — [TESTING.md](TESTING.md).

Результаты Zynq находятся в `ZYNQ7020/output/`: `hardware/` — bitstream и handoff, `diagnostic/` — тест платы, `linux/` — Linux и архив релиза, `logs/` — журналы. Сборки GUI располагаются в `ZYNQ7020/gui/output/<платформа>/`, Keil и проверки STM32 — в `STM32/output/`. Общего `output/` в корне репозитория нет.

## Windows

MSYS2 UCRT64: GCC, CMake и Ninja. SDL на Windows не нужен, используется существующий WinAPI frontend.

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cmake -S ZYNQ7020/gui -B ZYNQ7020/gui/output/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ZYNQ7020/gui/output/windows
ctest --test-dir ZYNQ7020/gui/output/windows --output-on-failure
./ZYNQ7020/gui/output/windows/scope_preview.exe
```

Также проверена сборка llvm-mingw 20240619 / Clang 18.1.8. У старого MinGW GCC 6.2 из Vivado 2019.1 отсутствуют import symbols для используемых исходным frontend WinAPI pointer/DPI функций; для Windows нужен современный toolchain.

## Настольный Linux

```sh
sudo apt install build-essential cmake ninja-build pkg-config libsdl2-dev
cmake -S ZYNQ7020/gui -B ZYNQ7020/gui/output/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ZYNQ7020/gui/output/linux
ctest --test-dir ZYNQ7020/gui/output/linux --output-on-failure
./ZYNQ7020/gui/output/linux/scope_preview --windowed
./ZYNQ7020/gui/output/linux/scope_preview --raw-demo --diagnostics
./ZYNQ7020/gui/output/linux/scope_preview --capture record.oscraw
```

Новые serial/raw/DRM параметры относятся к SDL frontend. `--serial /dev/ttyUSB0 --baud 115200` включает асинхронное управление. `--data-dir /media/scope` выбирает каталог существующего BMP/CSV хранилища. Штатный запуск дополнительно задаёт `--require-data-device`, чтобы разрешать экспорт только на файловую систему присутствующего блочного носителя. Raw-файл формируется через API `capture_file.h`; UI экспорта большого raw ещё не подключён. Демо raw не моделирует реальное поступление 400 МБ/с.

Параметры CMake: `SCOPE_BUILD_PREVIEW`, `SCOPE_BUILD_BENCHMARKS`, `BUILD_TESTING`; `SCOPE_ENABLE_DRM`, `SCOPE_ENABLE_FRAMEBUFFER` и `SCOPE_ENABLE_ZYNQ` выключены по умолчанию. Для прямого DRM нужны `libdrm-dev` и SDL2 с dummy video:

```sh
cmake -S ZYNQ7020/gui -B ZYNQ7020/gui/output/drm -G Ninja -DCMAKE_BUILD_TYPE=Release -DSCOPE_ENABLE_DRM=ON
cmake --build ZYNQ7020/gui/output/drm
./ZYNQ7020/gui/output/drm/scope_preview --drm /dev/dri/card0 --touch /dev/input/event0 --touch-rotate 0
```

Для простого framebuffer достаточно `-DSCOPE_ENABLE_FRAMEBUFFER=ON`, без libdrm: `scope_preview --fb /dev/fb0 --touch auto --diagnostics`. Требуется XRGB8888 1024×600; приложение проверяет формат и шаг строки через fbdev ioctl. `--touch auto` ищет прямой сенсор evdev: один контакт через ABS_X/Y + BTN_TOUCH или Type B с 1–32 слотами; можно указать конкретный путь. В штатном SDL выводе касания обрабатывает SDL; `--touch` предназначен для прямого Linux-вывода. Скрипт `S99oscill` берёт параметры из `/etc/default/oscill`. Framebuffer-приложение занимает tty2, при завершении возвращает прежнюю VT. Журнал загрузки идёт в UART; FSBL рисует логотип, который сохраняется до первого кадра GUI, поскольку framebuffer console отключена.

## ARMv7 Linux

Использовать согласованный hard-float toolchain/sysroot с target SDL2 и libdrm. Buildroot генерирует их в `<каталог сборки Buildroot>/host`; нельзя подключать библиотеки настольного ПК.

```sh
cmake -S ZYNQ7020/gui -B ZYNQ7020/gui/output/arm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=ZYNQ7020/linux/toolchain-armv7.cmake \
  -DSCOPE_TOOLCHAIN_PREFIX=/absolute/toolchain/bin/arm-linux-gnueabihf- \
  -DSCOPE_SYSROOT=/absolute/target/sysroot \
  -DSCOPE_ENABLE_DRM=ON -DSCOPE_ENABLE_ZYNQ=ON \
  -DBUILD_TESTING=OFF -DSCOPE_BUILD_BENCHMARKS=OFF
cmake --build ZYNQ7020/gui/output/arm
```

Если target-зависимости установлены отдельно от libc sysroot, указать `-DSCOPE_TARGET_PREFIX_PATH=/absolute/arm/staging` и `PKG_CONFIG_LIBDIR=/absolute/arm/staging/lib/pkgconfig`. Это явная сборочная зависимость, не поиск библиотек в соседних рабочих копиях. Toolchain задаёт Cortex-A9/NEON/VFP hard-float. `SCOPE_ENABLE_ZYNQ` компилирует userspace адаптер, но не создаёт отсутствующий kernel driver.

## Embedded Linux

Нужен Linux build host и исходники [Buildroot 2025.02.12](https://buildroot.org/downloads/buildroot-2025.02.12.tar.xz). SHA-256 этого tarball: `9c8b7a2a14c74c934ed656988c473b1c01d869cebb5b00525104cc8a65381fb4`. Пакеты host: build-essential, git, wget, cpio, unzip, rsync, bc, bison, flex, libssl-dev, libncurses-dev, file, python3. Установленный CMake 3.31.x позволяет Buildroot использовать его напрямую.

```sh
sh ZYNQ7020/linux/build.sh /absolute/buildroot-2025.02.12 /absolute/output/oscill-linux
```

`oscill_zynq_defconfig` собирает U-Boot 2024.10, ядро Xilinx 6.6.70, DTS, SquashFS для SD и initramfs с GUI для QSPI. Commit ядра — `3c22f2aad113cbbea5c6434c697d002a82826da9`; SHA-256 kernel/U-Boot archives заданы в `board/patches/`. Toolchain — Bootlin ARMv7 EABIHF glibc stable 2024.05-1, hash проверяет Buildroot. `oscill_userspace_defconfig` оставлен для отдельной сборки приложения/rootfs.

Rootfs-overlay задаёт автозапуск, настройки портов и ручное монтирование microSD. Первый комплект выводит GUI через `simple-framebuffer`, без libdrm/Mesa/X11/Wayland. Если в Buildroot включить libdrm, пакет также соберёт DRM backend. FSBL перед Linux настраивает тот же AXI VDMA/VTC/TMDS тракт. USB ULPI PHY использует драйвер Xilinx с `drv-vbus`; reset MIO46 выполняет сгенерированный `ps7_init`.

Для подготовки Linux BOOT.BIN после сборки аппаратной части:

```powershell
./ZYNQ7020/linux/package.ps1 -Images D:/build/oscill-linux/images
```

Для `sdcard.img` на Linux host нужны genimage, dosfstools и mtools. Genimage можно собрать командой `make -C /absolute/buildroot-2025.02.12 O=/absolute/output/oscill-linux host-genimage` и добавить `/absolute/output/oscill-linux/host/bin` в PATH:

```sh
sh ZYNQ7020/linux/assemble-sd.sh /absolute/buildroot/images /absolute/Linux-BOOT.BIN /absolute/release
```

Для QSPI FIT с ядром, DTB и initramfs создаётся сборкой как `image.bin`. Тот же каталог релиза упаковывается:

```sh
python3 ZYNQ7020/linux/assemble-qspi.py /absolute/release
```

Сборщик проверяет размеры и формирует `qspi.bin` на 16 МиБ: `BOOT.BIN` с адреса 0, `image.bin` с 0x520000. Лимиты файлов — 0x500000 и 0x9e0000 байт соответственно. Диапазон 0x500000–0x51ffff оставлен свободным; последние 1 МиБ от 0xf00000 отведены настройкам. U-Boot environment хранится в самом U-Boot. Это один системный слот, без A/B обновления.

После переноса собранного каталога в `ZYNQ7020/output/linux`:

```powershell
./ZYNQ7020/linux/release.ps1
```

Получается `ZYNQ7020/output/linux/oscill-linux-gui.zip` с SD/QSPI образами, первым запуском, размерами и SHA-256. `source-files.json` фиксирует хеши текущих исходников, включая незакоммиченные изменения; исходные Git SHA также указаны в manifest. Порядок запуска — [BRINGUP.md](BRINGUP.md).

Независимая проверка собранных файлов без устройства:

```sh
python3 ZYNQ7020/linux/tests/verify_release.py /absolute/release
```

Проверка Linux/GUI в QEMU 10.2.0 с SD-образом:

```sh
qemu-system-arm -M xilinx-zynq-a9,boot-mode=sd -m 512M -smp 1 \
  -kernel /absolute/buildroot/images/u-boot.elf \
  -drive file=/absolute/release/sdcard.img,if=sd,format=raw,snapshot=on \
  -display none -serial null -serial stdio -monitor none -nic none
```

Для QSPI в той же команде задать `boot-mode=qspi` и заменить `-drive` на `file=/absolute/release/qspi.bin,if=mtd,index=8,format=raw,snapshot=on`. Эмуляция начинает загрузку с U-Boot ELF; BootROM, FSBL и PL этим способом не проверяются. Остальные границы проверки — в [TESTING.md](TESTING.md).

## Аппаратный проект и тест платы

Windows, Vivado и SDK **2019.1**:

```powershell
./ZYNQ7020/build.ps1
```

Скрипт закрепляет Digilent rgb2dvi commit `f4613fff005b098065fd5d619a2b88e55720a423`, строит PS/DDR и HDMI, проверяет timing, затем собирает FSBL и тест DDR/HDMI в `ZYNQ7020/output/diagnostic/BOOT.BIN`. `-Output` меняет каталог результата, `-XilinxRoot` — корень установленного Xilinx. Выбран `xc7z020clg400-1` для консервативной проверки timing; `-Part` позволяет задать считанный с микросхемы суффикс.

Vivado открывается через `ZYNQ7020/oscill.xpr`; его generated-каталоги и workspace SDK `ZYNQ7020/oscill.sdk` расположены рядом. Bitstream и HDF экспортируются в `ZYNQ7020/output/hardware`. Исходники проекта остаются в `fpga/` и `standalone/`; повторная сборка через `build.ps1` генерирует проект заново.

JTAG-запуск того же теста с установленным hw_server:

```powershell
xsct ZYNQ7020/standalone/load-jtag.tcl D:/hobby/Oscill/ZYNQ7020/output
```

## RTL и STM32

Симуляция packer с Vivado 2019.1; каталог результата должен находиться вне `ZYNQ7020/fpga`:

```sh
vivado -mode batch -source ZYNQ7020/fpga/simulate.tcl -tclargs /absolute/output/rtl-test
```

HDMI bitstream строится отдельно от packer simulation. ADC constraints, clock/CDC, trigger и capture DMA ещё не подключены. Part `xc7z020clg400-1` не подтверждает speed grade установленного кристалла.

Прошивка STM32F103RCT6: открыть `STM32/oscill_controls.ioc` в CubeMX 6.12.1, выполнить Generate Code, затем Rebuild в `STM32/MDK-ARM/oscill_controls.uvprojx` с Arm Compiler 6. HAL/CMSIS находятся в проекте, результаты Keil — в `STM32/output/`. Распиновка, настройки и сохранение приложения при генерации — [STM32.md](STM32.md), запись — [FLASHING.md](FLASHING.md#stm32).

Переносимая обработка панели и UART-транспорт проверяются отдельно (`stm32_controls`, `stm32_controls_uart`):

```sh
cmake -S STM32 -B STM32/output/tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build STM32/output/tests
ctest --test-dir STM32/output/tests --output-on-failure
```

После генерации проверить конфигурацию и подключение приложения:

```sh
python STM32/tests/check_project.py
```

Фактически выполненные сборки и ограничение установленного Keil — [TESTING.md](TESTING.md).
