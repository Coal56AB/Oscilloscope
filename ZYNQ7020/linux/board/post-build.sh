#!/bin/sh
set -eu
chmod 755 "$1/etc/init.d/S99oscill" "$1/usr/sbin/oscill-card"
mkdir -p "$1/media/scope"
# SquashFS stays read-only, including on a read-only MTD partition.
sed -i '\|^::sysinit:/bin/mount -o remount,rw /$|d' "$1/etc/inittab"

# /dev must exist before devpts, including when booting an initramfs.
sed -i '/^devtmpfs[[:space:]]/d' "$1/etc/fstab"
if ! grep -q '^::sysinit:.*mount -t devtmpfs ' "$1/etc/inittab"; then
    sed -i '\|^::sysinit:/bin/mount -t proc proc /proc$|a\
::sysinit:/bin/mount -t devtmpfs -o mode=0755,nosuid devtmpfs /dev' "$1/etc/inittab"
fi
# Restore UART-only init after an incremental build of the earlier console image.
sed -i 's|::sysinit:/usr/sbin/oscill-boot-console|::sysinit:/etc/init.d/rcS|' "$1/etc/inittab"
rm -f "$1/usr/sbin/oscill-boot-console"
