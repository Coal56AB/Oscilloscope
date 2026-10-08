"""Check the fixed QSPI partition sizes; never write to a device."""
import argparse
from pathlib import Path
from qspi_layout import image_layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('boot', type=Path)
    parser.add_argument('fit', type=Path)
    args = parser.parse_args()
    try:
        layout = image_layout((('boot', args.boot), ('linux-fit', args.fit)))
    except ValueError as error:
        print(error)
        return 1
    for part in layout:
        print(f"{part['label']}: offset=0x{part['offset']:08x} bytes={part['bytes']} allocated={part['allocated']}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
