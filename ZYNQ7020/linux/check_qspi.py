"""Size budget only; this tool neither assembles nor writes flash images."""
import argparse
from pathlib import Path
from qspi_layout import ALIGN, FLASH_BYTES, SETTINGS_BYTES, align, image_layout

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flash-mib', type=int, default=FLASH_BYTES // (1024 * 1024))
    parser.add_argument('--settings-kib', type=int, default=SETTINGS_BYTES // 1024)
    parser.add_argument('--dual-system', action='store_true')
    parser.add_argument('boot', type=Path)
    parser.add_argument('kernel', type=Path)
    parser.add_argument('dtb', type=Path)
    parser.add_argument('rootfs', type=Path)
    args = parser.parse_args()
    if args.flash_mib <= 0 or args.settings_kib < 0:
        parser.error('Invalid capacity')
    parts = [('boot', args.boot), ('kernel', args.kernel), ('dtb', args.dtb), ('rootfs', args.rootfs)]
    if args.dual_system:
        parts += [('kernel-B', args.kernel), ('dtb-B', args.dtb), ('rootfs-B', args.rootfs)]
    try:
        layout, offset = image_layout(parts)
    except ValueError as error:
        parser.error(str(error))
    for part in layout:
        print(f"{part['label']}: offset=0x{part['offset']:08x} bytes={part['bytes']} aligned={part['allocated']}")
    # Two environment reserves and the final 64 KiB boot-script sector.
    offset += 3 * ALIGN + align(args.settings_kib * 1024)
    capacity = args.flash_mib * 1024 * 1024
    print(f'total={offset} capacity={capacity} remaining={capacity-offset}')
    return 0 if offset <= capacity else 1

if __name__ == '__main__':
    raise SystemExit(main())
