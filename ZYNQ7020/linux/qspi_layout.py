"""Shared erase-block allocation for the board's QSPI image tools."""
ALIGN = 64 * 1024
FLASH_BYTES = 16 * 1024 * 1024
SETTINGS_BYTES = 1024 * 1024


def align(value):
    return (value + ALIGN - 1) // ALIGN * ALIGN


def image_layout(parts):
    result, offset = [], 0
    for name, path in parts:
        size = path.stat().st_size
        if not size:
            raise ValueError(f'Empty image: {path}')
        allocated = align(size)
        result.append(dict(label=name, offset=offset, bytes=size, allocated=allocated))
        offset += allocated
    return result, offset
