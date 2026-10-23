#ifndef FIRMWARE_FLASH_H
#define FIRMWARE_FLASH_H
#include <stddef.h>
#include <stdint.h>
typedef void (*FirmwareProgress)(const char *phase, size_t done, size_t total, void *context);
uint32_t firmware_crc32(const void *data, size_t size);
int firmware_validate(const unsigned char *data, size_t size, int fit);
int firmware_flash(const char *boot, const char *fit, uint32_t boot_crc, uint32_t fit_crc,
                   FirmwareProgress progress, void *context);
#endif
