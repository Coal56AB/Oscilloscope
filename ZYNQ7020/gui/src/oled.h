#ifndef OSCILL_OLED_H
#define OSCILL_OLED_H
#include <stdint.h>
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ROWS 8
#define OLED_COLUMNS 21
/* Board-independent write-only, four-wire serial transport. CS is tied low. */
typedef struct {
    void *context;
    void (*write)(void *context, uint32_t lines);
    void (*delay_us)(void *context, unsigned us);
} OledBus;
typedef struct {
    OledBus bus;
    uint8_t pixels[1024], shadow[1024];
    uint32_t lines;
    int shadow_valid;
} Oled;
/* Explicit SSD1306 128x64 profile, as in Zynq_study; controller not identified by schematic. */
void oled_init(Oled *oled, OledBus bus);
void oled_text(Oled *oled, const char lines[OLED_ROWS][OLED_COLUMNS + 1]);
void oled_present(Oled *oled);
#endif
