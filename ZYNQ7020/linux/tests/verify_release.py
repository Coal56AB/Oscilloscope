"""Check BOOT.BIN, image.bin and qspi.bin without accessing a device."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qspi_layout import FLASH_BYTES, FIT_OFFSET, SETTINGS_BYTES, SETTINGS_OFFSET, image_layout


def require(condition, message):
    if not condition:
        raise ValueError(message)


def boot_header(data):
    require(len(data) >= 0xA0, 'Truncated Zynq boot header')
    require(struct.unpack_from('<II', data, 0x20) == (0xAA995566, 0x584C4E58), 'Zynq magic')
    words = struct.unpack_from('<11I', data, 0x20)
    require(sum(words) & 0xFFFFFFFF == 0xFFFFFFFF, 'Zynq header checksum')
    offset, length = struct.unpack_from('<II', data, 0x30)
    require(offset + length <= len(data), 'FSBL exceeds BOOT.BIN')


def main(directory):
    boot_header((directory / 'BOOT.BIN').read_bytes())
    names = ('BOOT.BIN', 'image.bin')
    layout = image_layout(zip(('boot', 'linux-fit'), (directory / name for name in names)))
    flash = (directory / 'qspi.bin').read_bytes()
    require(len(flash) == FLASH_BYTES, 'QSPI length')
    end = 0
    for part, name in zip(layout, names):
        data = (directory / name).read_bytes()
        start, allocated = part['offset'], part['allocated']
        require(start >= end and start % 65536 == 0 and allocated % 65536 == 0, 'QSPI alignment')
        require(flash[end:start] == b'\xff' * (start - end), 'QSPI reserve padding')
        require(len(data) == part['bytes'] <= allocated, 'QSPI partition length')
        require(flash[start:start + len(data)] == data, f'QSPI payload: {name}')
        end = start + allocated
        require(flash[start + len(data):end] == b'\xff' * (allocated - len(data)), 'QSPI padding')
    require(layout[0]['offset'] == 0 and layout[1]['offset'] == FIT_OFFSET,
            'QSPI boot/FIT offsets')
    require(end == SETTINGS_OFFSET and SETTINGS_BYTES == 0x100000,
            'QSPI settings layout')
    require(flash[end:] == b'\xff' * (len(flash) - end), 'QSPI settings padding')
    fit = (directory / 'image.bin').read_bytes()
    require(len(fit) >= 40, 'Truncated FIT header')
    require(struct.unpack_from('>I', fit)[0] == 0xd00dfeed, 'FIT header')
    require(struct.unpack_from('>I', fit, 4)[0] == len(fit), 'FIT length')
    print('PASS: Zynq header, FIT length, QSPI payloads/reserves')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: verify_release.py /path/to/release')
    main(Path(sys.argv[1]))
