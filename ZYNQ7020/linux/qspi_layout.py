"""Fixed QSPI layout shared by the image budget and assembly tools."""
ALIGN = 64 * 1024
FLASH_BYTES = 16 * 1024 * 1024
BOOT_BYTES = 0x500000
FIT_OFFSET = 0x520000
SETTINGS_BYTES = 1024 * 1024
SETTINGS_OFFSET = FLASH_BYTES - SETTINGS_BYTES
FIT_BYTES = SETTINGS_OFFSET - FIT_OFFSET


def image_layout(parts):
    parts = list(parts)
    if len(parts) != 2:
        raise ValueError('BOOT.BIN and image.bin required')
    result = []
    for (name, path), offset, allocated in zip(parts, (0, FIT_OFFSET), (BOOT_BYTES, FIT_BYTES)):
        size = path.stat().st_size
        if not size:
            raise ValueError(f'Empty image: {path}')
        if size > allocated:
            raise ValueError(f'{path.name} exceeds its QSPI partition: {size} > {allocated}')
        result.append(dict(label=name, offset=offset, bytes=size, allocated=allocated))
    return result
