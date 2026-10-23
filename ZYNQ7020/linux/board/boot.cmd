setenv bootargs console=ttyPS0,1000000 earlycon loglevel=4 usbcore.autosuspend=-1 uio_pdrv_genirq.of_id=oscill,oled-gpio root=/dev/mmcblk0p2 rootfstype=squashfs ro rootwait
if fatload mmc 0:1 0x03000000 zImage; then
    if fatload mmc 0:1 0x01800000 oscill-zynq7020.dtb; then
        bootz 0x03000000 - 0x01800000
    fi
fi
echo Oscill Linux files missing or boot failed
