#!/bin/sh
set -eu
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
VERSION=2025.02.12
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Windows checkouts do not preserve the executable bit of new shell scripts.
chmod +x "$SCRIPT_DIR/board/post-image.sh"
if [ "$#" -ne 2 ]; then echo 'Usage: build.sh /path/to/buildroot-2025.02.12 /absolute/output' >&2; exit 2; fi
SOURCE=$(CDPATH= cd -- "$1" && pwd)
OUTPUT=$2
case "$OUTPUT" in /*) ;; *) echo 'Output must be absolute' >&2; exit 2;; esac
grep -q "^export BR2_VERSION := $VERSION$" "$SOURCE/Makefile" || { echo "Buildroot $VERSION required" >&2; exit 1; }
make -C "$SOURCE" O="$OUTPUT" BR2_EXTERNAL="$SCRIPT_DIR" oscill_zynq_defconfig
make -C "$SOURCE" O="$OUTPUT" BR2_EXTERNAL="$SCRIPT_DIR"
for OPTION in CONFIG_FB_SIMPLE CONFIG_FB_DEVICE CONFIG_VT_CONSOLE \
    CONFIG_INPUT_EVDEV CONFIG_USB_CHIPIDEA_HOST CONFIG_USB_CHIPIDEA_GENERIC \
    CONFIG_USB_ULPI CONFIG_USB_HID CONFIG_HID_MULTITOUCH; do
    grep -qx "$OPTION=y" "$OUTPUT/build/linux-custom/.config" || {
        echo "Required kernel option missing: $OPTION=y" >&2
        exit 1
    }
done
grep -qx '# CONFIG_FRAMEBUFFER_CONSOLE is not set' "$OUTPUT/build/linux-custom/.config" || {
    echo 'Framebuffer console would overwrite the boot splash' >&2
    exit 1
}
IMAGE_SIZE=$(stat -c '%s' "$OUTPUT/images/image.bin")
[ "$IMAGE_SIZE" -le $((0x9e0000)) ] || { echo 'image.bin exceeds the QSPI partition' >&2; exit 1; }
