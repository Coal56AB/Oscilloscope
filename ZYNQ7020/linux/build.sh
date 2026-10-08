#!/bin/sh
set -eu
VERSION=2025.02.12
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 2 ]; then echo 'Usage: build.sh /path/to/buildroot-2025.02.12 /absolute/output' >&2; exit 2; fi
SOURCE=$(CDPATH= cd -- "$1" && pwd)
OUTPUT=$2
case "$OUTPUT" in /*) ;; *) echo 'Output must be absolute' >&2; exit 2;; esac
grep -q "^export BR2_VERSION := $VERSION$" "$SOURCE/Makefile" || { echo "Buildroot $VERSION required" >&2; exit 1; }
make -C "$SOURCE" O="$OUTPUT" BR2_EXTERNAL="$SCRIPT_DIR" oscill_zynq_defconfig
make -C "$SOURCE" O="$OUTPUT" BR2_EXTERNAL="$SCRIPT_DIR"
