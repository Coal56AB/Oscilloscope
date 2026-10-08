#!/bin/sh
set -eu
PATH="$PATH:/usr/sbin:/sbin"
export PATH
if [ "$#" -ne 3 ]; then
    echo 'Usage: assemble-sd.sh /absolute/buildroot/images /absolute/Linux-BOOT.BIN /absolute/release' >&2
    exit 2
fi
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGES=$(CDPATH= cd -- "$1" && pwd)
BOOT=$2
RELEASE=$3
case "$BOOT:$RELEASE" in /*:/*) ;; *) echo 'Absolute paths required' >&2; exit 2;; esac
for file in boot.scr zImage oscill-zynq7020.dtb rootfs.squashfs; do
    [ -s "$IMAGES/$file" ] || { echo "Missing $IMAGES/$file" >&2; exit 1; }
done
[ -s "$BOOT" ] || { echo 'Linux BOOT.BIN missing' >&2; exit 1; }
for tool in genimage mkdosfs mcopy; do command -v "$tool" >/dev/null; done
mkdir -p "$RELEASE"
TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM
mkdir "$TEMP/input" "$TEMP/root"
for file in boot.scr zImage oscill-zynq7020.dtb rootfs.squashfs; do
    cp "$IMAGES/$file" "$TEMP/input/$file"
done
cp "$BOOT" "$TEMP/input/BOOT.BIN"
genimage --rootpath "$TEMP/root" --tmppath "$TEMP/work" \
    --inputpath "$TEMP/input" --outputpath "$RELEASE" --config "$HERE/board/genimage.cfg"
cp "$TEMP/input/"* "$RELEASE/"
(cd "$RELEASE" && sha256sum BOOT.BIN boot.scr zImage oscill-zynq7020.dtb rootfs.squashfs sdcard.img > SHA256SUMS)
