"""Assemble the fixed 16 MiB QSPI image without accessing hardware."""
import argparse
import hashlib
import json
from pathlib import Path
from qspi_layout import FLASH_BYTES, SETTINGS_BYTES, SETTINGS_OFFSET, image_layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', type=Path, help='Release containing BOOT.BIN and image.bin')
    args = parser.parse_args()
    images = args.images.resolve()
    paths = [images / name for name in ('BOOT.BIN', 'image.bin')]
    layout = image_layout(zip(('boot', 'linux-fit'), paths))
    image = bytearray(b'\xff') * FLASH_BYTES
    for part, path in zip(layout, paths):
        data = path.read_bytes()
        if len(data) != part['bytes']:
            raise ValueError(f'{path} changed during packaging')
        image[part['offset']:part['offset'] + len(data)] = data
        part['sha256'] = hashlib.sha256(data).hexdigest()
    manifest = dict(flash_bytes=FLASH_BYTES, settings_offset=SETTINGS_OFFSET,
                    settings_bytes=SETTINGS_BYTES,
                    free_bytes=layout[1]['allocated'] - layout[1]['bytes'],
                    partitions=layout, hardware_tested=False,
                    image_sha256=hashlib.sha256(image).hexdigest())
    (images / 'qspi-layout.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (images / 'qspi.bin').write_bytes(image)
    print(f"QSPI image: {images / 'qspi.bin'}; FIT free {manifest['free_bytes']} bytes")


if __name__ == '__main__':
    main()
