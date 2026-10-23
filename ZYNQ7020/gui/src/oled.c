#include "oled.h"
#include "oled_font.h"
#include <string.h>

static void output(Oled *o, uint32_t value)
{
    o->lines = value;
    o->bus.write(o->bus.context, value);
}

static void byte(Oled *o, uint8_t value, int data)
{
    int bit;
    output(o, 8U | (data ? 4U : 0U));
    for (bit = 7; bit >= 0; --bit) {
        uint32_t lines = (o->lines & ~3U) | (((value >> bit) & 1U) << 1);
        output(o, lines);
        o->bus.delay_us(o->bus.context, 1);
        output(o, lines | 1U);
        o->bus.delay_us(o->bus.context, 1);
    }
    output(o, o->lines & ~1U);
}

void oled_init(Oled *o, OledBus bus)
{
    static const uint8_t init[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0, 0x40, 0x8d, 0x14,
        0x20, 0, 0xa1, 0xc8, 0xda, 0x12, 0x81, 0x7f, 0xd9, 0xf1,
        0xdb, 0x40, 0xa4, 0xa6, 0x2e, 0xaf
    };
    unsigned i;
    memset(o, 0, sizeof(*o));
    o->bus = bus;
    output(o, 0);
    bus.delay_us(bus.context, 10000);
    output(o, 8);
    bus.delay_us(bus.context, 10000);
    for (i = 0; i < sizeof(init); ++i) byte(o, init[i], 0);
}

void oled_text(Oled *o, const char lines[OLED_ROWS][OLED_COLUMNS + 1])
{
    unsigned row, col, x;
    memset(o->pixels, 0, sizeof(o->pixels));
    for (row = 0; row < OLED_ROWS; ++row)
        for (col = 0; col < OLED_COLUMNS && lines[row][col]; ++col) {
            const uint8_t *glyph = Font5x7_GetGlyph(lines[row][col]);
            if (glyph) for (x = 0; x < 5; ++x)
                o->pixels[row * OLED_WIDTH + col * 6 + x] = glyph[x];
        }
    /* Inverted title, with one dark separator row. */
    for (x = 0; x < OLED_WIDTH; ++x) o->pixels[x] ^= 0x7f;
}

void oled_present(Oled *o)
{
    unsigned page, x;
    for (page = 0; page < 8; ++page) {
        unsigned first = 128, last = 0, offset = page * 128;
        for (x = 0; x < 128; ++x)
            if (!o->shadow_valid || o->pixels[offset+x] != o->shadow[offset+x]) {
                if (first == 128) first = x;
                last = x;
            }
        if (first == 128) continue;
        byte(o, 0x21, 0); byte(o, (uint8_t)first, 0); byte(o, (uint8_t)last, 0);
        byte(o, 0x22, 0); byte(o, (uint8_t)page, 0); byte(o, (uint8_t)page, 0);
        for (x = first; x <= last; ++x) byte(o, o->pixels[offset+x], 1);
        memcpy(o->shadow+offset+first, o->pixels+offset+first, last-first+1);
    }
    o->shadow_valid = 1;
}
