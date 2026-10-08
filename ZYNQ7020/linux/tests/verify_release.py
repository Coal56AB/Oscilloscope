"""Check assembled SD/QSPI files without accessing a device."""
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path


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


def script_header(data):
    magic, header_crc, _, length, _, _, payload_crc = struct.unpack_from('>7I', data)
    require(magic == 0x27051956 and length == len(data) - 64, 'U-Boot script header')
    header = bytearray(data[:64])
    header[4:8] = bytes(4)
    require(zlib.crc32(header) == header_crc, 'U-Boot script header CRC')
    require(zlib.crc32(data[64:]) == payload_crc, 'U-Boot script data CRC')


def main(directory):
    boot_header((directory / 'BOOT.BIN').read_bytes())
    for name in ('boot.scr', 'qspi.scr'):
        script_header((directory / name).read_bytes())
    rootfs = (directory / 'rootfs.squashfs').read_bytes()
    require(rootfs[:4] == b'hsqs', 'SquashFS magic')
    sd = (directory / 'sdcard.img').read_bytes()
    require(len(sd) == 128 * 1024 * 1024 and sd[510:512] == b'\x55\xaa', 'SD size/MBR')
    partitions = []
    for index in range(2):
        entry = sd[446 + index * 16:462 + index * 16]
        start, count = struct.unpack_from('<II', entry, 8)
        partitions.append((entry[4], start * 512, count * 512))
    fat, root = partitions
    require(fat[0] == 0x0C and root[0] == 0x83, 'SD partition types')
    require(fat[1] == 1024 * 1024 and fat[1] + fat[2] == root[1], 'SD partition offsets')
    require(root[1] + root[2] == len(sd), 'SD partition extent')
    bpb = sd[fat[1]:fat[1] + 512]
    require(bpb[82:90] == b'FAT32   ' and bpb[22:24] == bytes(2), 'FAT32 BPB')
    require(sd[root[1]:root[1] + len(rootfs)] == rootfs, 'SD rootfs payload')
    layout = json.loads((directory / 'qspi-layout.json').read_text())
    flash = (directory / 'qspi.bin').read_bytes()
    require(len(flash) == layout['flash_bytes'] == 16 * 1024 * 1024, 'QSPI length')
    require(hashlib.sha256(flash).hexdigest() == layout['image_sha256'], 'QSPI hash')
    names = ('BOOT.BIN', 'zImage', 'oscill-zynq7020-qspi.dtb', 'rootfs.squashfs')
    end = 0
    for part, name in zip(layout['partitions'], names):
        data = (directory / name).read_bytes()
        start, allocated = part['offset'], part['allocated']
        require(start == end and start % 65536 == 0 and allocated % 65536 == 0, 'QSPI alignment')
        require(len(data) == part['bytes'] <= allocated, 'QSPI partition length')
        require(flash[start:start + len(data)] == data, f'QSPI payload: {name}')
        require(hashlib.sha256(data).hexdigest() == part['sha256'], f'QSPI hash: {name}')
        end = start + allocated
        require(flash[start + len(data):end] == b'\xff' * (allocated - len(data)), 'QSPI padding')
    script = (directory / 'qspi.scr').read_bytes()
    start = layout['script_offset']
    require(end <= layout['settings_offset'] and layout['settings_offset'] + layout['settings_bytes'] <= start,
            'QSPI settings overlap')
    require(flash[end:start] == b'\xff' * (start - end), 'QSPI reserves/free space')
    require(flash[start:start + len(script)] == script, 'QSPI script payload')
    require(flash[start + len(script):] == b'\xff' * (len(flash) - start - len(script)), 'QSPI script padding')
    print('PASS: Zynq header, script CRCs, MBR/FAT32, SD rootfs, QSPI payloads/reserves')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: verify_release.py /path/to/release')
    main(Path(sys.argv[1]))
