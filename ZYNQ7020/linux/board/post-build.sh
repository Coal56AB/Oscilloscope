#!/bin/sh
set -eu
chmod 755 "$1/etc/init.d/S99oscill" "$1/usr/sbin/oscill-card"
mkdir -p "$1/media/scope"
# SquashFS stays read-only, including on a read-only MTD partition.
sed -i '\|^::sysinit:/bin/mount -o remount,rw /$|d' "$1/etc/inittab"
