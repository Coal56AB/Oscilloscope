# Сборка `OscilGUI-TinkerBoard.zip`

Скрипт `build.sh` создаёт именно готовый переносимый пакет для ASUS Tinker Board. Он кросс-компилирует приложение для `armv7l` и `aarch64`, собирает совместимую SDL2 2.0.8, добавляет лаунчер и лицензии, рассчитывает контрольные суммы и формирует один архив:

```text
OscilGUI-TinkerBoard.zip
```

Промежуточные файлы создаются во временном каталоге и удаляются автоматически.

## Подготовка WSL

Откройте Ubuntu в WSL и установите инструменты:

```sh
sudo apt update
sudo apt install gcc-arm-linux-gnueabihf gcc-aarch64-linux-gnu make curl zip libx11-dev libxext-dev qemu-user
```

Для максимальной совместимости со старым TinkerOS рекомендуется Ubuntu 18.04 с GCC 7.5. Более новый кросс-компилятор может потребовать более новую версию `glibc` на плате.

## Сборка

Перейдите из WSL в каталог проекта и запустите скрипт:

```sh
cd /mnt/d/OtherStuff/Oscil/GUI
./tinker_board/build.sh
```

После успешной сборки готовый архив находится здесь:

```text
D:\OtherStuff\Oscil\GUI\OscilGUI-TinkerBoard.zip
```

Если установлен `qemu-user`, скрипт дополнительно запускает обе ARM-версии без окна, создаёт контрольные снимки и проверяет, что ARM32 и ARM64 отрисовывают одинаковый экран.

## Содержимое архива

```text
OscilGUI-TinkerBoard/
  START.desktop
  run.sh
  README.txt
  SHA256SUMS.txt
  bin/armhf/scope_preview
  bin/arm64/scope_preview
  lib/armhf/libSDL2-2.0.so.0
  lib/arm64/libSDL2-2.0.so.0
  licenses/Inter.txt
  licenses/SDL2.txt
```

На Tinker Board распакуйте архив на флешку и дважды нажмите `START.desktop`. Устанавливать компилятор или SDL2 на плату не требуется.
