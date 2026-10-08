setenv bootargs console=ttyPS0,115200 earlycon loglevel=7 usbcore.autosuspend=-1 root=/dev/mmcblk0p2 rootfstype=squashfs ro rootwait
if fatload mmc 0:1 0x03000000 zImage; then
    if fatload mmc 0:1 0x01800000 oscill-zynq7020.dtb; then
        bootz 0x03000000 - 0x01800000
    fi
fi
echo Oscill Linux files missing or boot failed
