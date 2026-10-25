"""Assemble the QSPI firmware area, excluding persistent settings."""
import argparse
from pathlib import Path
from qspi_layout import SETTINGS_OFFSET, image_layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', type=Path, help='Directory containing BOOT.BIN and image.bin')
    args = parser.parse_args()
    images = args.images.resolve()
    paths = [images / name for name in ('BOOT.BIN', 'image.bin')]
    layout = image_layout(zip(('boot', 'linux-fit'), paths))
    image = bytearray(b'\xff') * SETTINGS_OFFSET
    for part, path in zip(layout, paths):
        data = path.read_bytes()
        if len(data) != part['bytes']:
            raise ValueError(f'{path} changed during packaging')
        image[part['offset']:part['offset'] + len(data)] = data
    (images / 'qspi.bin').write_bytes(image)
    print(f"QSPI image: {images / 'qspi.bin'}; FIT free {layout[1]['allocated'] - layout[1]['bytes']} bytes")


if __name__ == '__main__':
    main()
