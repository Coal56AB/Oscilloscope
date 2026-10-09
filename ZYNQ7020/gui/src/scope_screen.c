#include "scope_screen.h"
#include "scope_font_smooth.h"
#include "rounded_box.h"
#include "boot_splash.h"
#include <assert.h>

#include <stddef.h>
#include <string.h>

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define PLOT_X SCOPE_PLOT_X
#define PLOT_Y SCOPE_PLOT_Y

static uint32_t *frame;
static int pitch;
static int view_top;
static int view_height;
static int active_font;
static int active_rounding;
static int draw_alpha = 255;
static RoundedBox rounded_layers[8];
static int rounded_depth;

static int view_y(int plot_y)
{
    return view_top + plot_y * view_height / SCOPE_PLOT_HEIGHT;
}

static const uint8_t glyphs[53][7] = {
    {0,0,0,0,0,0,0},            /* space */
    {14,17,19,21,25,17,14},     /* 0 */
    {4,12,4,4,4,4,14},          /* 1 */
    {14,17,1,2,4,8,31},         /* 2 */
    {30,1,1,14,1,1,30},         /* 3 */
    {2,6,10,18,31,2,2},        /* 4 */
    {31,16,16,30,1,1,30},      /* 5 */
    {14,16,16,30,17,17,14},    /* 6 */
    {31,1,2,4,8,8,8},          /* 7 */
    {14,17,17,14,17,17,14},    /* 8 */
    {14,17,17,15,1,1,14},      /* 9 */
    {14,17,17,31,17,17,17},    /* A */
    {30,17,17,30,17,17,30},    /* B */
    {14,17,16,16,16,17,14},    /* C */
    {30,17,17,17,17,17,30},    /* D */
    {31,16,16,30,16,16,31},    /* E */
    {31,16,16,30,16,16,16},    /* F */
    {14,17,16,23,17,17,15},    /* G */
    {17,17,17,31,17,17,17},    /* H */
    {14,4,4,4,4,4,14},         /* I */
    {7,2,2,2,18,18,12},        /* J */
    {17,18,20,24,20,18,17},    /* K */
    {16,16,16,16,16,16,31},    /* L */
    {17,27,21,21,17,17,17},    /* M */
    {17,25,21,19,17,17,17},    /* N */
    {14,17,17,17,17,17,14},    /* O */
    {30,17,17,30,16,16,16},    /* P */
    {14,17,17,17,21,18,13},    /* Q */
    {30,17,17,30,20,18,17},    /* R */
    {15,16,16,14,1,1,30},      /* S */
    {31,4,4,4,4,4,4},          /* T */
    {17,17,17,17,17,17,14},    /* U */
    {17,17,17,17,17,10,4},     /* V */
    {17,17,17,21,21,21,10},    /* W */
    {17,17,10,4,10,17,17},     /* X */
    {17,17,10,4,4,4,4},        /* Y */
    {31,1,2,4,8,16,31},        /* Z */
    {0,0,0,0,0,0,4},            /* . */
    {0,4,0,0,4,0,0},            /* : */
    {0,0,0,31,0,0,0},           /* - */
    {0,0,4,14,4,0,0},           /* + */
    {1,2,2,4,8,8,16},          /* / */
    {0,0,17,10,4,10,17},       /* * */
    {0,0,17,10,4,10,17},       /* x */
    {0,0,0,0,0,0,31},          /* _ */
    {0,0,17,17,17,19,13},      /* u */
    {0,0,15,16,14,1,30},       /* s */
    {16,16,18,20,24,20,18},    /* k */
    {0,0,31,2,4,8,31},         /* z */
    {0,0,26,21,21,21,21},      /* m */
    {25,26,4,8,11,19,0},       /* % */
    {0,0,30,17,17,17,17},     /* n */
    {1,1,15,17,17,17,15}      /* d */
};

static int glyph_index(char c)
{
    if (c == 'x') return 43;
    if (c == 'u') return 45;
    if (c == 's') return 46;
    if (c == 'k') return 47;
    if (c == 'z') return 48;
    if (c == 'm') return 49;
    if (c == '%') return 50;
    if (c == 'n') return 51;
    if (c == 'd') return 52;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return 1 + c - '0';
    if (c >= 'A' && c <= 'Z') return 11 + c - 'A';
    switch (c) {
        case '.': return 37;
        case ':': return 38;
        case '-': return 39;
        case '+': return 40;
        case '/': return 41;
        case '*': return 42;
        case '_': return 44;
        default: return 0;
    }
}

static void pixel(int x, int y, uint32_t color)
{
    if ((unsigned)x < SCOPE_WIDTH && (unsigned)y < SCOPE_HEIGHT) {
        uint32_t *target = &frame[(size_t)y * pitch + x];
        if (draw_alpha == 255) *target = color;
        else {
            uint32_t background = *target;
            int r = (((color >> 16) & 255) * draw_alpha +
                     ((background >> 16) & 255) * (255 - draw_alpha)) / 255;
            int g = (((color >> 8) & 255) * draw_alpha +
                     ((background >> 8) & 255) * (255 - draw_alpha)) / 255;
            int b = ((color & 255) * draw_alpha +
                     (background & 255) * (255 - draw_alpha)) / 255;
            *target = RGB(r, g, b);
        }
    }
}

static void fill(int x, int y, int w, int h, uint32_t color)
{
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCOPE_WIDTH) w = SCOPE_WIDTH - x;
    if (y + h > SCOPE_HEIGHT) h = SCOPE_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    if (draw_alpha == 255) {
        for (yy = y; yy < y + h; ++yy)
            for (xx = x; xx < x + w; ++xx)
                frame[(size_t)yy * pitch + xx] = color;
    } else {
        for (yy = y; yy < y + h; ++yy)
            for (xx = x; xx < x + w; ++xx)
                pixel(xx, yy, color);
    }
}

static void line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        int e2;
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static const SmoothGlyph *smooth_glyph(int font_index, int scale, int index)
{
    (void)font_index;
    return scale == 2 ? &inter_glyphs_2[index] :
           scale == 3 ? &inter_glyphs_3[index] : &inter_glyphs_4[index];
}

static const uint8_t *smooth_alpha(int font_index, int scale)
{
    (void)font_index;
    return scale == 2 ? inter_alpha_2 :
           scale == 3 ? inter_alpha_3 : inter_alpha_4;
}

static int text_width_mode(const char *s, int scale, int font_index)
{
    int width = 0;
    if (!s) return 0;
    if (scale < SCOPE_MIN_TEXT_SCALE) scale = SCOPE_MIN_TEXT_SCALE;
    for (; *s; ++s)
        width += font_index ?
                 smooth_glyph(font_index, scale, glyph_index(*s))->advance :
                 6 * scale;
    return width;
}

static int text_width(const char *s, int scale)
{
    return text_width_mode(s, scale, active_font);
}

static int cursor_readout_width(const ScopeScreen *screen, int index)
{
    int row, value_width = 0;
    for (row = 0; row < screen->cursor_measurement_rows && row < SCOPE_CURSOR_ROWS; ++row) {
        int width = text_width_mode(screen->cursor_measurement_values[row][index], 2, screen->font_index);
        if (width > value_width) value_width = width;
    }
    return 36 + text_width_mode(screen->cursor_measurement_labels[index], 2, screen->font_index) + value_width;
}

static const char *cursor_title(const ScopeScreen *screen, int row)
{
    return screen->cursor_mode == SCOPE_CURSOR_TIME ? "TIME" : row ? "CH2" : "CH1";
}

static int cursor_title_width(const ScopeScreen *screen)
{
    int row, width = 64;
    for (row = 0; row < screen->cursor_measurement_rows && row < SCOPE_CURSOR_ROWS; ++row) {
        int needed = text_width_mode(cursor_title(screen, row), 2, screen->font_index) + 24;
        if (needed > width) width = needed;
    }
    return width;
}

int scope_screen_cursor_measurement_width(const ScopeScreen *screen)
{
    int count, width, i;
    if (!screen || screen->cursor_measurement_rows <= 0 || screen->cursor_measurement_count <= 0) return 0;
    width = cursor_title_width(screen);
    count = screen->cursor_measurement_count;
    if (count > SCOPE_CURSOR_READOUTS) count = SCOPE_CURSOR_READOUTS;
    for (i = 0; i < count; ++i) width += cursor_readout_width(screen, i);
    return width > SCOPE_WIDTH - 8 ? SCOPE_WIDTH - 8 : width;
}

void scope_screen_cursor_measurement_bounds(const ScopeScreen *screen, int *x, int *y,
                                            int *width, int *height)
{
    int rows = screen ? screen->cursor_measurement_rows : 0;
    if (rows < 0) rows = 0;
    if (rows > SCOPE_CURSOR_ROWS) rows = SCOPE_CURSOR_ROWS;
    if (x) *x = 0;
    if (y) *y = SCOPE_MEASURE_BOTTOM_Y - rows * SCOPE_ZOOM_CURSOR_BAND;
    if (width) *width = scope_screen_cursor_measurement_width(screen);
    if (height) *height = rows ? rows * SCOPE_ZOOM_CURSOR_BAND - 2 : 0;
}

int scope_screen_measurement_bottom(const ScopeScreen *screen, int x, int width)
{
    int cursor_x, cursor_y, cursor_width, cursor_height;
    scope_screen_cursor_measurement_bounds(screen, &cursor_x, &cursor_y, &cursor_width, &cursor_height);
    return cursor_height > 0 && x < cursor_x + cursor_width && x + width > cursor_x ?
           cursor_y : SCOPE_MEASURE_BOTTOM_Y;
}

static void text(int x, int y, const char *s, int scale, uint32_t color)
{
    if (!s) return;
    if (scale < SCOPE_MIN_TEXT_SCALE) scale = SCOPE_MIN_TEXT_SCALE;
    for (; *s; ++s) {
        int index = glyph_index(*s);
        const uint8_t *g = glyphs[index];
        int row, col;
        if (!active_font) {
            for (row = 0; row < 7; ++row)
                for (col = 0; col < 5; ++col)
                    if (g[row] & (16 >> col))
                        fill(x + col * scale, y + row * scale, scale, scale, color);
            x += 6 * scale;
        } else {
            int py, px;
            const SmoothGlyph *bitmap = smooth_glyph(active_font, scale, index);
            const uint8_t *alpha_data = smooth_alpha(active_font, scale) + bitmap->offset;
            for (py = 0; py < bitmap->height; ++py)
                for (px = 0; px < bitmap->width; ++px) {
                int alpha = alpha_data[py * bitmap->width + px] * draw_alpha / 255;
                int sx = x + bitmap->left + px, sy = y + bitmap->top + py;
                uint32_t background;
                int r, gg, b;
                if (alpha <= 0 || (unsigned)sx >= SCOPE_WIDTH ||
                    (unsigned)sy >= SCOPE_HEIGHT) continue;
                background = frame[(size_t)sy * pitch + sx];
                r = (((color >> 16) & 255) * alpha +
                     ((background >> 16) & 255) * (255 - alpha)) / 255;
                gg = (((color >> 8) & 255) * alpha +
                      ((background >> 8) & 255) * (255 - alpha)) / 255;
                b = ((color & 255) * alpha +
                     (background & 255) * (255 - alpha)) / 255;
                frame[(size_t)sy * pitch + sx] = RGB(r, gg, b);
            }
            x += bitmap->advance;
        }
    }
}

static void frame_rect(int x, int y, int w, int h, uint32_t color)
{
    fill(x, y, w, 1, color);
    fill(x, y + h - 1, w, 1, color);
    fill(x, y, 1, h, color);
    fill(x + w - 1, y, 1, h, color);
}

static void rounded_fill(int x, int y, int w, int h,
                         uint32_t color)
{
    assert(rounded_depth < 8);
    rounded_box_begin(&rounded_layers[rounded_depth++], frame, pitch,
                      SCOPE_WIDTH, SCOPE_HEIGHT, x, y, w, h, active_rounding);
    fill(x, y, w, h, color);
}

static void rounded_frame(int x, int y, int w, int h,
                          uint32_t color)
{
    RoundedBox *box = &rounded_layers[rounded_depth - 1];
    assert(box->x == x && box->y == y && box->width == w && box->height == h);
    box->border = color;
    box->border_alpha = draw_alpha;
    frame_rect(x, y, w, h, color);
}

static void rounded_done(void)
{
    assert(rounded_depth > 0);
    rounded_box_end(&rounded_layers[--rounded_depth]);
}

static void action_box(int x, int width, const char *label, uint32_t accent)
{
    int label_width = text_width(label, 2);
    rounded_fill(x, 2, width, 46, RGB(12, 19, 44));
    rounded_frame(x, 2, width, 46, RGB(75, 92, 142));
    fill(x + 1, 3, width - 2, 3, accent);
    text(x + (width - label_width) / 2, 18, label, 2, accent);
    rounded_done();
}

static void navigation_box(const ScopeScreen *screen, uint32_t color, uint32_t green)
{
    rounded_fill(4, 2, 180, 46, RGB(12, 19, 44));
    rounded_frame(4, 2, 180, 46, RGB(75, 92, 142));
    fill(5, 3, 178, 3, color);
    fill(20, 14, 26, 3, color);
    fill(20, 24, 26, 3, color);
    fill(20, 34, 26, 3, color);
    text(61, 15, "MENU", 3, color);
    frame_rect(155, 13, 19, 28, color);
    fill(161, 9, 7, 4, color);
    if (screen->battery_known) {
        int level = screen->battery_percent > 100 ? 100 : screen->battery_percent;
        int height = level * 22 / 100;
        fill(158, 38 - height, 13, height,
             level < 20 ? RGB(237, 85, 95) : green);
        if (screen->battery_charging) fill(164, 19, 2, 14, color);
    } else {
        fill(159, 33, 11, 5, RGB(96, 111, 136));
    }
    rounded_done();
}

static void acquisition_box(int running, int waveform_loaded)
{
    const char *label = waveform_loaded ? "BACK" : running ? "RUN" : "STOP";
    uint32_t color = waveform_loaded ? RGB(255, 196, 77) :
                     running ? RGB(112, 223, 163) : RGB(237, 85, 95);
    rounded_fill(856, 2, 164, 46, color);
    rounded_frame(856, 2, 164, 46, color);
    text(856 + (164 - text_width(label, 4)) / 2, 11, label, 4, RGB(12, 19, 44));
    rounded_done();
}

static int measurement_channel(const char *label)
{
    if (label && label[0] == 'C' && label[1] == 'H') {
        if (label[2] == '1') return 0;
        if (label[2] == '2') return 1;
    }
    return -1;
}

static const char *measurement_display_label(const char *label)
{
    return measurement_channel(label) >= 0 && label[3] == ' ' ? label + 4 : label;
}

static int measurement_column_width(const ScopeScreen *screen, int count,
                                    int column, int *label_width)
{
    int i, widest_label = 0, widest_value = 0;
    int horizontal = screen->measurement_horizontal;
    int columns = horizontal ? (count < 5 ? count : 5) : (count + 4) / 5;
    for (i = 0; i < count; ++i) {
        int item_column = horizontal ? i % columns : i / 5;
        if (item_column == column) {
            int label = text_width_mode(measurement_display_label(
                                        screen->measurement_labels[i]), 2,
                                        screen->font_index);
            int value = text_width_mode(screen->measurement_values[i], 2,
                                        screen->font_index);
            if (label > widest_label) widest_label = label;
            if (value > widest_value) widest_value = value;
        }
    }
    if (label_width) *label_width = widest_label;
    return horizontal ? widest_label + widest_value + 16 :
                        widest_label + widest_value + 28;
}

void scope_screen_measurement_bounds(const ScopeScreen *screen, int *x, int *y,
                                     int *width, int *height)
{
    int count = screen->measurement_hidden ? 0 : screen->measurement_count;
    int available_bottom;
    int w, h, left, top;
    if (screen->measurement_hidden) {
        int header_width = 11 + text_width_mode("MEASUREMENTS", 2,
                                                screen->font_index) +
                           12 + 64 + 5;
        if (header_width < 230) header_width = 230;
        if (x) *x = 0;
        if (y) *y = scope_screen_measurement_bottom(screen, 0, header_width) - 30;
        if (width) *width = header_width;
        if (height) *height = 30;
        return;
    }
    if (count < 0) count = 0;
    if (count > SCOPE_MEASURE_SLOTS) count = SCOPE_MEASURE_SLOTS;
    w = 0;
    if (count > 0) {
        int column, columns = screen->measurement_horizontal ?
            (count < 5 ? count : 5) : (count + 4) / 5;
        for (column = 0; column < columns; ++column)
            w += measurement_column_width(screen, count, column, NULL);
    }
    if (count > 0 && screen->measurement_horizontal) {
        int columns = count < 5 ? count : 5;
        if (count >= 5) w = 984;
        else {
            if (w < columns * 180) w = columns * 180;
            if (w < 260) w = 260;
            if (w > 984) w = 984;
        }
    } else if (w < 230) w = 230;
    {
        int button_width = count > 0 && screen->measurement_horizontal ? 90 : 64;
        int header_width = 11 + text_width_mode("MEASUREMENTS", 2,
                                                screen->font_index) +
                           12 + button_width + 5;
        if (w < header_width) w = header_width;
    }
    h = screen->measurement_horizontal ? 30 + ((count + 4) / 5) * 32 :
        30 + (count < 5 ? count : 5) * 32;
    left = screen->measurement_x;
    top = screen->measurement_y;
    if (left < 0) left = 0;
    if (left > SCOPE_WIDTH - w) left = SCOPE_WIDTH - w;
    available_bottom = scope_screen_measurement_bottom(screen, left, w);
    if (top < SCOPE_PLOT_Y) top = SCOPE_PLOT_Y;
    if (top > available_bottom - h) top = available_bottom - h;
    if (x) *x = left;
    if (y) *y = top;
    if (width) *width = w;
    if (height) *height = h;
}

static void label_chip(int x, int width, const char *label, uint32_t color)
{
    fill(x, 540, width, 19, color);
    frame_rect(x, 540, width, 19, color);
    text(x + 8, 543, label, 2, RGB(8, 10, 19));
}

static void bottom_value(int x, const char *value, uint32_t color)
{
    text(x, 566, value, 3, color);
}

static void cursor_card_value(int x, const ScopeScreen *screen, uint32_t muted)
{
    uint32_t color = RGB(182, 150, 241);
    bottom_value(x,
                 screen->cursor_mode == SCOPE_CURSOR_TIME ? "TIME" :
                 screen->cursor_mode == SCOPE_CURSOR_VOLTAGE ? "VERTICAL" : "OFF",
                 screen->cursor_mode == SCOPE_CURSOR_OFF ? muted : color);
    if (screen->cursor_mode != SCOPE_CURSOR_OFF) {
        const char *selected = screen->cursor_selected == SCOPE_CURSOR_SELECT_FFT ? "FFT" :
                               screen->cursor_selected == SCOPE_CURSOR_SELECT_B ? "B" : "A";
        text(1008 - text_width(selected, 2), 543, selected, 2, color);
    }
}

static void mode_icon(int x, int position_mode, int horizontal, uint32_t color)
{
    if (position_mode) {
        int major, minor;
        for (major = 0; major <= 32; ++major) {
            for (minor = -6; minor <= 6; ++minor) {
                int inset = (minor < 0 ? -minor : minor) * 8 / 6;
                if ((major >= 9 && major <= 23 && minor >= -1 && minor <= 1) ||
                    (major >= inset && major <= 8) ||
                    (major >= 24 && major <= 32 - inset)) {
                    pixel(horizontal ? x + 6 + major : x + 21 + minor,
                          horizontal ? 576 + minor : 559 + major, color);
                }
            }
        }
    } else {
        fill(x + 4, 568, 11, 3, color);
        fill(x + 8, 564, 3, 11, color);
        line(x + 17, 586, x + 28, 564, color);
        line(x + 18, 586, x + 29, 564, color);
        fill(x + 30, 583, 10, 3, color);
    }
}

static void single_edge_icon(int x, int y, int falling, uint32_t color)
{
    int row;
    fill(x, y + (falling ? 2 : 12), 4, 2, color);
    fill(x + 4, y + 2, 2, 12, color);
    fill(x + 6, y + (falling ? 12 : 2), 4, 2, color);
    for (row = 0; row < 3; ++row) {
        int width = falling ? 6 - row * 2 : 2 + row * 2;
        fill(x + 5 - width / 2, y + (falling ? 8 : 5) + row, width, 1, color);
    }
}

static void trigger_edge_icon(int x, int y, int falling, int both, uint32_t color)
{
    if (both) {
        single_edge_icon(x, y, 0, color);
        single_edge_icon(x + 10, y, 1, color);
    } else single_edge_icon(x + 5, y, falling, color);
}

static void trigger_level_handle(int y, int preview, uint32_t color)
{
    int row;
    int right = SCOPE_WIDTH - 2;
    if (preview) {
        line(right, y - 11, right - 24, y, color);
        line(right, y + 11, right - 24, y, color);
        line(right, y - 11, right, y + 11, color);
    } else {
        for (row = -11; row <= 11; ++row) {
            int width = 26 - 2 * (row < 0 ? -row : row);
            fill(right - width + 1, y + row, width, 1, color);
        }
    }
}

static void channel_zero_marker(int y, const char *label, uint32_t background, uint32_t color)
{
    int row;
    fill(0, y - 12, 25, 25, background);
    for (row = -12; row <= 12; ++row) {
        int width = 13 - (row < 0 ? -row : row);
        fill(25, y + row, width, 1, background);
    }
    text(9, y - 7, label, 2, color);
}

static void trace_segment(int x0, int a, int x1, int b, uint32_t shadow, uint32_t color)
{
    if (x0 == x1) {
        pixel(x0, a - 1, shadow);
        pixel(x0, b + 1, shadow);
        fill(x0, a, 1, b - a + 1, color);
        return;
    }
    line(x0, a - 1, x1, b - 1, shadow);
    line(x0, a + 1, x1, b + 1, shadow);
    line(x0, a, x1, b, color);
}

static int trace_y(int y)
{
    if (y < 3) y = 3;
    if (y > SCOPE_PLOT_HEIGHT - 4) y = SCOPE_PLOT_HEIGHT - 4;
    return view_y(y);
}

static void trace(const int16_t *samples, uint32_t shadow, uint32_t color)
{
    int x;
    if (!samples) return;
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x) {
        trace_segment(PLOT_X + x - 1, trace_y(samples[x - 1]),
                      PLOT_X + x, trace_y(samples[x]), shadow, color);
    }
}

static void trace_envelope(const int16_t *minimum, const int16_t *maximum,
                            uint32_t shadow, uint32_t color)
{
    int x, previous_low = 0, previous_high = 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        int low = trace_y(minimum[x]), high = trace_y(maximum[x]);
        trace_segment(PLOT_X + x, low, PLOT_X + x, high, shadow, color);
        /* Connect only gaps between ranges; a midpoint trace creates false beats. */
        if (x && previous_high < low)
            trace_segment(PLOT_X + x - 1, previous_high, PLOT_X + x, low, shadow, color);
        else if (x && previous_low > high)
            trace_segment(PLOT_X + x - 1, previous_low, PLOT_X + x, high, shadow, color);
        previous_low = low;
        previous_high = high;
    }
}

static uint32_t density_color(int level)
{
    static const struct { int level, r, g, b; } palette[] = {
        {0, 0, 0, 0}, {16, 0, 35, 150}, {48, 0, 120, 255},
        {96, 0, 235, 230}, {144, 40, 240, 70},
        {190, 250, 230, 15}, {225, 255, 110, 10}, {255, 255, 25, 20}
    };
    int i;
    if (level > 255) level = 255;
    for (i = 1; i < (int)(sizeof(palette) / sizeof(palette[0])); ++i) {
        if (level <= palette[i].level) {
            int span = palette[i].level - palette[i - 1].level;
            int offset = level - palette[i - 1].level;
            int r = palette[i - 1].r +
                    (palette[i].r - palette[i - 1].r) * offset / span;
            int g = palette[i - 1].g +
                    (palette[i].g - palette[i - 1].g) * offset / span;
            int b = palette[i - 1].b +
                    (palette[i].b - palette[i - 1].b) * offset / span;
            return RGB(r, g, b);
        }
    }
    return RGB(255, 25, 20);
}

static void trace_density(const int16_t *history, const int16_t *minimum,
                          const int16_t *maximum, int count)
{
    enum { RADIUS = 18 };
    int frame_index, x, y;
    uint16_t density[SCOPE_PLOT_HEIGHT];
    if (!history || count <= 0) return;
    if (count > 8) count = 8;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        int first = SCOPE_PLOT_HEIGHT, last = 0;
        memset(density, 0, sizeof(density));
        for (frame_index = 0; frame_index < count; ++frame_index) {
            const int16_t *samples = history + frame_index * SCOPE_PLOT_WIDTH;
            int index = frame_index * SCOPE_PLOT_WIDTH + x;
            int low = minimum ? minimum[index] : samples[x];
            int high = maximum ? maximum[index] : samples[x];
            int previous = x ? samples[x - 1] : samples[x];
            int offset;
            if (low < 3) low = 3;
            if (low > SCOPE_PLOT_HEIGHT - 4) low = SCOPE_PLOT_HEIGHT - 4;
            if (high < 3) high = 3;
            if (high > SCOPE_PLOT_HEIGHT - 4) high = SCOPE_PLOT_HEIGHT - 4;
            if (previous < 3) previous = 3;
            if (previous > SCOPE_PLOT_HEIGHT - 4) previous = SCOPE_PLOT_HEIGHT - 4;
            for (offset = -RADIUS; offset <= high - low + RADIUS; ++offset) {
                int target = low + offset;
                int distance = offset < 0 ? -offset : target > high ? target - high : 0;
                if (target < 0 || target >= SCOPE_PLOT_HEIGHT) continue;
                density[target] += (uint16_t)(RADIUS + 1 - distance);
                if (target < first) first = target;
                if (target > last) last = target;
            }
            if (previous < low || previous > high) {
                int start = previous < low ? previous : high;
                int end = previous < low ? low : previous;
                for (y = start; y <= end; ++y) density[y] += 3;
                if (start < first) first = start;
                if (end > last) last = end;
            }
        }
        for (y = first; y <= last; ++y) {
            int level = density[y] * 255 / (count * (RADIUS + 1));
            if (level > 0)
                pixel(PLOT_X + x, view_y(y), density_color(level));
        }
    }
}

static void overview_trace(const int16_t *samples, int start, int end,
                           int height, uint32_t color)
{
    int x;
    if (!samples) return;
    if (start < 1) start = 1;
    if (end > SCOPE_PLOT_WIDTH) end = SCOPE_PLOT_WIDTH;
    for (x = start; x < end; ++x) {
        int a = PLOT_Y + 7 + samples[x - 1] *
                (height - 14) / SCOPE_PLOT_HEIGHT;
        int b = PLOT_Y + 7 + samples[x] *
                (height - 14) / SCOPE_PLOT_HEIGHT;
        line(PLOT_X + x - 1, a, PLOT_X + x, b, color);
    }
}

int scope_screen_fft_level_at(const ScopeScreen *screen, int y)
{
    int height = screen->split_height - 9;
    int span = height - 23;
    int level = span > 0 ? ((SCOPE_PLOT_Y + height - 5 - y) * 255 + span / 2) / span : 0;
    return level < 0 ? 0 : level > 255 ? 255 : level;
}

static void spectrum_trace(const uint8_t *bins, int height, uint32_t color)
{
    int i;
    if (!bins) return;
    for (i = 1; i < SCOPE_FFT_BINS; ++i) {
        int x0 = (i - 1) * (SCOPE_PLOT_WIDTH - 1) / (SCOPE_FFT_BINS - 1);
        int x1 = i * (SCOPE_PLOT_WIDTH - 1) / (SCOPE_FFT_BINS - 1);
        int y0 = PLOT_Y + height - 5 - bins[i - 1] * (height - 23) / 255;
        int y1 = PLOT_Y + height - 5 - bins[i] * (height - 23) / 255;
        line(x0, y0, x1, y1, color);
    }
}

void scope_screen_render(uint32_t *pixels, int stride, const ScopeScreen *screen)
{
    const uint32_t yellow = RGB(255, 196, 77);
    const uint32_t cyan = RGB(82, 210, 235);
    const uint32_t green = RGB(112, 223, 163);
    const uint32_t white = RGB(229, 240, 242);
    const uint32_t muted = RGB(125, 150, 162);
    uint32_t trigger_color;
    int i, ty;

    if (!pixels || !screen || stride < SCOPE_WIDTH) return;
    assert(rounded_depth == 0);
    trigger_color = screen->trigger_source_channel ? cyan : yellow;
    frame = pixels;
    pitch = stride;
    draw_alpha = 255;
    active_font = screen->font_index < SCOPE_FONT_COUNT ?
                  screen->font_index : SCOPE_FONT_PIXEL;
    active_rounding = SCOPE_PANEL_RADIUS;
    if (screen->browser_visible) {
        int y;
        if (screen->browser_pixels)
            for (y = 0; y < SCOPE_HEIGHT; ++y)
                memcpy(frame + (size_t)y * pitch,
                       screen->browser_pixels + (size_t)y * SCOPE_WIDTH,
                       SCOPE_WIDTH * sizeof(uint32_t));
        else fill(0, 0, SCOPE_WIDTH, SCOPE_HEIGHT, RGB(8, 10, 19));
        fill(0, SCOPE_BOTTOM_Y, SCOPE_WIDTH, SCOPE_BOTTOM_HEIGHT,
             RGB(15, 20, 38));
        fill(0, SCOPE_BOTTOM_Y, SCOPE_WIDTH, 2, RGB(105, 119, 173));
        rounded_fill(12, 546, 140, 44, RGB(38, 48, 73));
        rounded_frame(12, 546, 140, 44, RGB(105, 119, 173));
        text(82 - text_width("LEFT", 2) / 2, 561, "LEFT", 2, white);
        rounded_done();
        rounded_fill(160, 546, 140, 44, RGB(38, 48, 73));
        rounded_frame(160, 546, 140, 44, RGB(105, 119, 173));
        text(230 - text_width("RIGHT", 2) / 2, 561, "RIGHT", 2, white);
        rounded_done();
        text(318, 561, screen->browser_title ? screen->browser_title : "", 2, cyan);
        rounded_fill(872, 546, 140, 44, RGB(38, 48, 73));
        rounded_frame(872, 546, 140, 44, RGB(105, 119, 173));
        text(942 - text_width("BACK", 2) / 2, 561, "BACK", 2, white);
        rounded_done();
        return;
    }
    {
        int split = screen->split_height;
        int cursor_band = 0;
        if (split < 70) split = 70;
        if (split > 250) split = 250;
        view_top = screen->zoom_enabled || screen->fft_enabled ?
                   PLOT_Y + split + cursor_band : PLOT_Y;
        view_height = screen->zoom_enabled || screen->fft_enabled ?
                      SCOPE_PLOT_HEIGHT - split - cursor_band : SCOPE_PLOT_HEIGHT;
    }

    fill(0, 0, SCOPE_WIDTH, SCOPE_HEIGHT, BOOT_SPLASH_BACKGROUND);
    fill(0, 0, SCOPE_WIDTH, 50, RGB(23, 33, 76));
    navigation_box(screen, white, green);
    action_box(188, 180, "SCREENSHOT", white);
    action_box(372, 176, "SAVE WAVE", white);
    action_box(552, 140, screen->fine_mode ? "FINE" : "COARSE",
               screen->fine_mode ? green : white);
    action_box(696, 156, "PROCESSING", white);
    acquisition_box(screen->running, screen->waveform_loaded);

    fill(0, 50, SCOPE_WIDTH, SCOPE_BOTTOM_Y - 50, RGB(8, 9, 16));
    fill(PLOT_X, PLOT_Y, SCOPE_PLOT_WIDTH, SCOPE_PLOT_HEIGHT, RGB(7, 8, 14));
    if (screen->zoom_enabled) {
        const char *zoom_label = screen->zoom_label ? screen->zoom_label : "ZOOM";
        int overview_height = screen->split_height - 26;
        int start = screen->zoom_window_start;
        int end = screen->zoom_window_end;
        if (start < 0) start = 0;
        if (end >= SCOPE_PLOT_WIDTH) end = SCOPE_PLOT_WIDTH - 1;
        if (end < start) end = start;
        fill(PLOT_X, PLOT_Y, SCOPE_PLOT_WIDTH, overview_height, RGB(10, 17, 35));
        fill(PLOT_X + start, PLOT_Y + 1, end - start + 1,
             overview_height - 2, RGB(24, 40, 76));
        overview_trace(screen->capture_ch1_samples, 1, SCOPE_PLOT_WIDTH,
                       overview_height, RGB(119, 85, 34));
        overview_trace(screen->capture_ch2_samples, 1, SCOPE_PLOT_WIDTH,
                       overview_height, RGB(36, 91, 103));
        overview_trace(screen->capture_ch1_samples, start, end + 1,
                       overview_height, yellow);
        overview_trace(screen->capture_ch2_samples, start, end + 1,
                       overview_height, cyan);
        frame_rect(PLOT_X + start, PLOT_Y + 1, end - start + 1,
                   overview_height - 2, RGB(124, 153, 227));
        fill(PLOT_X, PLOT_Y + overview_height + 1,
             SCOPE_PLOT_WIDTH, screen->split_height - overview_height - 2,
             RGB(31, 38, 64));
        text(PLOT_X + 9, PLOT_Y + overview_height + 3,
             "FULL CAPTURE", 2, white);
        text(PLOT_X + SCOPE_PLOT_WIDTH - 12 - text_width(zoom_label, 2),
             PLOT_Y + overview_height + 3, zoom_label, 2, yellow);
        fill(0, PLOT_Y + screen->split_height - 2,
             SCOPE_WIDTH, 4, RGB(126, 143, 185));
        fill(492, PLOT_Y + screen->split_height - 5,
             40, 10, RGB(126, 143, 185));
        fill(PLOT_X, view_top, SCOPE_PLOT_WIDTH, view_height, RGB(7, 8, 14));
    } else if (screen->fft_enabled) {
        int upper = screen->split_height - 9;
        int chart_height = upper;
        fill(0, PLOT_Y, SCOPE_WIDTH, upper, RGB(10, 17, 35));
        for (i = 1; i < 8; ++i)
            fill(i * SCOPE_WIDTH / 8, PLOT_Y, 1, chart_height, RGB(39, 47, 70));
        for (i = 1; i < 4; ++i)
            fill(0, PLOT_Y + i * chart_height / 4, SCOPE_WIDTH, 1, RGB(39, 47, 70));
        spectrum_trace(screen->ch1_enabled ? screen->fft_ch1_bins : NULL, chart_height, yellow);
        spectrum_trace(screen->ch2_enabled ? screen->fft_ch2_bins : NULL, chart_height, cyan);
        fill(0, view_top - 9, SCOPE_WIDTH, 9, RGB(31, 38, 64));
        fill(492, view_top - 11, 40, 12, RGB(126, 143, 185));
        text(8, PLOT_Y + 5, "FFT", 2, white);
        text(64, PLOT_Y + 5, screen->fft_sampling_info, 2, muted);
        for (i = 0; i < SCOPE_FFT_TICKS; ++i) {
            int x = i * (SCOPE_WIDTH - 1) / (SCOPE_FFT_TICKS - 1);
            const char *label = screen->fft_frequency_labels[i];
            int label_width = text_width(label, 2);
            int left = x - label_width / 2;
            if (left < 8) left = 8;
            if (left + label_width > SCOPE_WIDTH - 8) left = SCOPE_WIDTH - 8 - label_width;
            fill(x, PLOT_Y + upper - 5, 1, 4, muted);
            text(left, PLOT_Y + upper - 17, label, 2, white);
        }
        if (screen->fft_cursor_visible) {
            uint32_t color = RGB(207, 183, 255);
            int x = screen->fft_cursor_x;
            int width = text_width(screen->fft_cursor_value, 2) + 12;
            int left;
            if (x < 0) x = 0;
            if (x >= SCOPE_PLOT_WIDTH) x = SCOPE_PLOT_WIDTH - 1;
            if (screen->cursor_mode == SCOPE_CURSOR_VOLTAGE) {
                int level = screen->fft_cursor_level;
                int y, label_y;
                if (level < 0) level = 0;
                if (level > 255) level = 255;
                y = PLOT_Y + upper - 5 - level * (upper - 23) / 255;
                label_y = y - 22;
                if (label_y < PLOT_Y + 22) label_y = y + 3;
                if (label_y > PLOT_Y + upper - 37) label_y = PLOT_Y + upper - 37;
                if (label_y < PLOT_Y + 22) label_y = PLOT_Y + 22;
                for (i = 1; i < SCOPE_WIDTH; i += 10)
                    fill(i, y, SCOPE_WIDTH - i < 5 ? SCOPE_WIDTH - i : 5, 1, color);
                pixel(SCOPE_WIDTH - 1, y, color);
                fill(SCOPE_WIDTH - width - 8, label_y, width, 20, RGB(27, 25, 45));
                text(SCOPE_WIDTH - width - 2, label_y + 3, screen->fft_cursor_value, 2, color);
            } else {
                left = x + 8;
                if (left + width > SCOPE_WIDTH - 4) left = x - width - 8;
                if (left < 4) left = 4;
                for (i = 1; i < upper; i += 10)
                    fill(x, PLOT_Y + i, 1, upper - i < 5 ? upper - i : 5, color);
                pixel(x, PLOT_Y + upper - 1, color);
                fill(left, PLOT_Y + 22, width, 20, RGB(27, 25, 45));
                text(left + 6, PLOT_Y + 25, screen->fft_cursor_value, 2, color);
            }
        }
    }
    if (screen->grid_enabled) {
        int j;
        for (i = 1; i < 10; ++i) {
            int x = PLOT_X + i * SCOPE_PLOT_WIDTH / 10;
            for (j = 0; j < view_height; j += 8)
                fill(x, view_top + j, 1, i == 5 ? 4 : 2,
                     i == 5 ? RGB(70, 74, 93) : RGB(42, 44, 59));
        }
        for (i = 1; i < 8; ++i) {
            int y = view_top + i * view_height / 8;
            for (j = 0; j < SCOPE_PLOT_WIDTH; j += 8)
                fill(PLOT_X + j, y, i == 4 ? 4 : 2, 1,
                     i == 4 ? RGB(70, 74, 93) : RGB(42, 44, 59));
        }
    }

    {
        int marker = screen->trigger_marker_x;
        int row;
        if (marker < 0) marker = 0;
        if (marker >= SCOPE_PLOT_WIDTH) marker = SCOPE_PLOT_WIDTH - 1;
        fill(PLOT_X + marker, view_top, 1, view_height, RGB(66, 69, 86));
        for (row = 0; row < 10; ++row) {
            int half_width = (10 - row) * 7 / 10;
            fill(PLOT_X + marker - half_width, view_top + 2 + row,
                 2 * half_width + 1, 1, yellow);
        }
    }
    ty = screen->trigger_y;
    if (ty < 8) ty = 8;
    if (ty > SCOPE_PLOT_HEIGHT - 9) ty = SCOPE_PLOT_HEIGHT - 9;
    ty = view_y(ty);
    fill(PLOT_X, ty, SCOPE_PLOT_WIDTH, 1, trigger_color);
    if (screen->trigger_preview) {
        int py = screen->trigger_preview_y;
        if (py < 8) py = 8;
        if (py > SCOPE_PLOT_HEIGHT - 9) py = SCOPE_PLOT_HEIGHT - 9;
        py = view_y(py);
        for (i = 0; i < SCOPE_PLOT_WIDTH; i += 14)
            fill(PLOT_X + i, py, 7, 1, trigger_color);
    }
    if (screen->ch1_enabled) {
        if (screen->intensity_coloring && !screen->zoom_enabled &&
            screen->history_ch1_samples && screen->history_count > 0)
            trace_density(screen->history_ch1_samples, screen->history_ch1_min_samples,
                          screen->history_ch1_max_samples, screen->history_count);
        else if (screen->ch1_min_samples && screen->ch1_max_samples)
            trace_envelope(screen->ch1_min_samples, screen->ch1_max_samples, RGB(89, 64, 29), yellow);
        else trace(screen->ch1_samples, RGB(89, 64, 29), yellow);
    }
    if (screen->ch2_enabled) {
        if (screen->intensity_coloring && !screen->zoom_enabled &&
            screen->history_ch2_samples && screen->history_count > 0)
            trace_density(screen->history_ch2_samples, screen->history_ch2_min_samples,
                          screen->history_ch2_max_samples, screen->history_count);
        else if (screen->ch2_min_samples && screen->ch2_max_samples)
            trace_envelope(screen->ch2_min_samples, screen->ch2_max_samples, RGB(27, 66, 78), cyan);
        else trace(screen->ch2_samples, RGB(27, 66, 78), cyan);
    }
    if (screen->cursor_mode != SCOPE_CURSOR_OFF) {
        const uint32_t cursor = RGB(207, 183, 255);
        if (screen->cursor_mode == SCOPE_CURSOR_TIME) {
            int a = screen->cursor_a, b = screen->cursor_b;
            if (a < 0) a = 0;
            if (a >= SCOPE_PLOT_WIDTH) a = SCOPE_PLOT_WIDTH - 1;
            if (b < 0) b = 0;
            if (b >= SCOPE_PLOT_WIDTH) b = SCOPE_PLOT_WIDTH - 1;
            for (i = 0; i < view_height; i += 12) {
                fill(PLOT_X + a, view_top + i, 1, 6, cursor);
                fill(PLOT_X + b, view_top + i, 1, 6, cursor);
            }
            fill(PLOT_X + a + 3, view_top + 4, 24, 23, RGB(65, 48, 84));
            fill(PLOT_X + b + 3, view_top + 4, 24, 23, RGB(65, 48, 84));
            text(PLOT_X + a + 9, view_top + 8, "A", 2, cursor);
            text(PLOT_X + b + 9, view_top + 8, "B", 2, cursor);
        } else {
            int a = screen->cursor_a, b = screen->cursor_b;
            if (a < 0) a = 0;
            if (a >= SCOPE_PLOT_HEIGHT) a = SCOPE_PLOT_HEIGHT - 1;
            if (b < 0) b = 0;
            if (b >= SCOPE_PLOT_HEIGHT) b = SCOPE_PLOT_HEIGHT - 1;
            for (i = 0; i < SCOPE_PLOT_WIDTH; i += 12) {
                fill(PLOT_X + i, view_y(a), 6, 1, cursor);
                fill(PLOT_X + i, view_y(b), 6, 1, cursor);
            }
            fill(PLOT_X + 3, view_y(a) - 13, 24, 24, RGB(65, 48, 84));
            fill(PLOT_X + 3, view_y(b) - 13, 24, 24, RGB(65, 48, 84));
            text(PLOT_X + 9, view_y(a) - 8, "A", 2, cursor);
            text(PLOT_X + 9, view_y(b) - 8, "B", 2, cursor);
        }
    }
    frame_rect(PLOT_X, PLOT_Y, SCOPE_PLOT_WIDTH, SCOPE_PLOT_HEIGHT, RGB(86, 89, 118));
    if (screen->ch1_enabled) {
        int y = screen->channel_zero_y[0];
        if (y < 10) y = 10;
        if (y > SCOPE_PLOT_HEIGHT - 15) y = SCOPE_PLOT_HEIGHT - 15;
        channel_zero_marker(view_y(y), "1", RGB(119, 91, 26), yellow);
    }
    if (screen->ch2_enabled) {
        int y = screen->channel_zero_y[1];
        if (y < 10) y = 10;
        if (y > SCOPE_PLOT_HEIGHT - 15) y = SCOPE_PLOT_HEIGHT - 15;
        channel_zero_marker(view_y(y), "2", RGB(27, 91, 106), cyan);
    }
    trigger_level_handle(ty, 0, trigger_color);
    if (screen->trigger_preview) {
        int py = screen->trigger_preview_y;
        if (py < 8) py = 8;
        if (py > SCOPE_PLOT_HEIGHT - 9) py = SCOPE_PLOT_HEIGHT - 9;
        trigger_level_handle(view_y(py), 1, trigger_color);
    }
    active_rounding = SCOPE_WINDOW_RADIUS;
    {
        int count = screen->measurement_hidden ? 0 : screen->measurement_count;
        int left, top, width, height;
        if (count > SCOPE_MEASURE_SLOTS) count = SCOPE_MEASURE_SLOTS;
        scope_screen_measurement_bounds(screen, &left, &top, &width, &height);
        rounded_fill(left, top, width, height, RGB(18, 21, 35));
        rounded_frame(left, top, width, height, RGB(105, 119, 173));
        rounded_fill(left + 1, top + 1, width - 2, 28, RGB(35, 42, 70));
        text(left + 11, top + 8, "MEASUREMENTS", 2, white);
        {
            int button_width = count > 0 && screen->measurement_horizontal ? 90 : 64;
            rounded_fill(left + width - button_width - 5, top + 3,
                         button_width, 24, RGB(56, 64, 94));
            rounded_frame(left + width - button_width - 5, top + 3,
                          button_width, 24, RGB(126, 143, 185));
        }
        {
            const char *layout = screen->measurement_hidden ? "SHOW" : count == 0 ? "ADD" :
                                 screen->measurement_horizontal ? "COLUMN" : "ROW";
            int button_width = count > 0 && screen->measurement_horizontal ? 90 : 64;
            text(left + width - button_width - 5 +
                 (button_width - text_width(layout, 2)) / 2,
                 top + 8, layout, 2, white);
            rounded_done();
            rounded_done();
        }
        for (i = 0; i < count; ++i) {
            const char *label = measurement_display_label(screen->measurement_labels[i]);
            int channel = measurement_channel(screen->measurement_labels[i]);
            uint32_t color = white;
            if (channel >= 0) color = channel ? cyan : yellow;
            if (screen->measurement_horizontal) {
                int columns = count < 5 ? count : 5;
                int column = i % columns;
                int row = i / columns;
                int prior, before = 0, natural_total = 0;
                int cell_x, cell_w, label_w;
                int cell_y = top + 30 + row * 32;
                for (prior = 0; prior < column; ++prior)
                    before += measurement_column_width(screen, count, prior, NULL);
                for (prior = 0; prior < columns; ++prior)
                    natural_total += measurement_column_width(screen, count, prior, NULL);
                cell_x = left + before * width / natural_total;
                cell_w = left + (before + measurement_column_width(screen, count,
                         column, &label_w)) * width / natural_total - cell_x;
                fill(cell_x + 2, cell_y + 2, cell_w - 4, 28, RGB(23, 28, 47));
                if (column) fill(cell_x, cell_y, 1, 31, RGB(78, 89, 125));
                if (row) fill(cell_x + 1, cell_y, cell_w - 2, 1, RGB(78, 89, 125));
                text(cell_x + 4, cell_y + 10, label, 2, color);
                text(cell_x + label_w + 11, cell_y + 10,
                     screen->measurement_values[i], 2, white);
            } else {
                int column = i / 5;
                int row = top + 30 + (i % 5) * 32;
                int prior, cell_x = left, label_w;
                int cell_w = measurement_column_width(screen, count, column, &label_w);
                for (prior = 0; prior < column; ++prior)
                    cell_x += measurement_column_width(screen, count, prior, NULL);
                if (column == (count + 4) / 5 - 1) cell_w = left + width - cell_x;
                if (i % 5) fill(cell_x + 1, row, cell_w - 2, 1, RGB(78, 89, 125));
                if (column && i % 5 == 0)
                    fill(cell_x, top + 30, 1, height - 31, RGB(78, 89, 125));
                text(cell_x + 11, row + 10, label, 2, color);
                text(cell_x + label_w + 19, row + 10,
                     screen->measurement_values[i], 2, white);
            }
        }
        rounded_done();
    }
    if (screen->cursor_measurement_rows > 0) {
        int row, strip_y, width, height;
        scope_screen_cursor_measurement_bounds(screen, NULL, &strip_y, &width, &height);
        rounded_fill(0, strip_y, width, height, RGB(27, 25, 45));
        rounded_frame(0, strip_y, width, height, RGB(153, 126, 196));
        for (row = 0; row < screen->cursor_measurement_rows && row < SCOPE_CURSOR_ROWS; ++row) {
            int x = cursor_title_width(screen);
            int y = strip_y + row * SCOPE_ZOOM_CURSOR_BAND;
            uint32_t color = RGB(207, 183, 255);
            if (row) fill(1, y - 1, width - 2, 1, RGB(100, 84, 137));
            text(12, y + 9, cursor_title(screen, row), 2, color);
            for (i = 0; i < screen->cursor_measurement_count && i < SCOPE_CURSOR_READOUTS; ++i) {
                int label_width = text_width(screen->cursor_measurement_labels[i], 2);
                fill(x, y + 1, 1, row ? 29 : height - 2, RGB(100, 84, 137));
                text(x + 10, y + 9, screen->cursor_measurement_labels[i], 2, color);
                text(x + 22 + label_width, y + 9, screen->cursor_measurement_values[row][i], 2, white);
                x += cursor_readout_width(screen, i);
            }
        }
        rounded_done();
    }
    active_rounding = SCOPE_PANEL_RADIUS;
    if (screen->waveform_loaded) {
        static const int tile_x[] = {306, 484, 662, 840};
        static const int tile_w[] = {172, 172, 172, 178};
        const uint32_t tile_colors[] = {
            screen->ch1_enabled ? yellow : RGB(96, 101, 112),
            screen->ch2_enabled ? cyan : RGB(96, 101, 112),
            white, RGB(182, 150, 241)
        };
        for (i = 0; i < 4; ++i) {
            int disabled = (i == 0 && !screen->ch1_enabled) ||
                           (i == 1 && !screen->ch2_enabled);
            rounded_fill(tile_x[i], SCOPE_BOTTOM_Y, tile_w[i],
                         SCOPE_BOTTOM_HEIGHT,
                         disabled ? RGB(27, 31, 39) : RGB(8, 10, 19));
            rounded_frame(tile_x[i], SCOPE_BOTTOM_Y, tile_w[i],
                          SCOPE_BOTTOM_HEIGHT,
                          disabled ? RGB(79, 85, 96) : RGB(94, 106, 142));
            fill(tile_x[i] + 1, SCOPE_BOTTOM_Y + 1,
                 tile_w[i] - 2, 3,
                 tile_colors[i]);
        }
        rounded_fill(12, 546, 140, 44, RGB(38, 48, 73));
        rounded_frame(12, 546, 140, 44, RGB(105, 119, 173));
        text(82 - text_width("LEFT", 2) / 2, 561, "LEFT", 2, white);
        rounded_done();
        rounded_fill(160, 546, 140, 44, RGB(38, 48, 73));
        rounded_frame(160, 546, 140, 44, RGB(105, 119, 173));
        text(230 - text_width("RIGHT", 2) / 2, 561, "RIGHT", 2, white);
        rounded_done();
        label_chip(306, 54, "CH1", screen->ch1_enabled ? yellow : RGB(79, 85, 96));
        if (screen->ch1_enabled) text(377, 543, screen->ch1_input, 2, white);
        bottom_value(318, screen->ch1_enabled ? screen->ch1_scale : "OFF",
                     screen->ch1_enabled ? yellow : muted);
        mode_icon(430, screen->ch_position_mode[0], 0,
                  screen->ch1_enabled ? yellow : muted);
        label_chip(484, 54, "CH2", screen->ch2_enabled ? cyan : RGB(79, 85, 96));
        if (screen->ch2_enabled) text(555, 543, screen->ch2_input, 2, white);
        bottom_value(496, screen->ch2_enabled ? screen->ch2_scale : "OFF",
                     screen->ch2_enabled ? cyan : muted);
        mode_icon(608, screen->ch_position_mode[1], 0,
                  screen->ch2_enabled ? cyan : muted);
        label_chip(662, 65, "TIME", white);
        bottom_value(674, screen->time_scale, white);
        mode_icon(786, screen->time_position_mode, 1, white);
        label_chip(840, 106, "CURSORS", RGB(182, 150, 241));
        cursor_card_value(852, screen, muted);
        for (i = 0; i < 4; ++i) rounded_done();
    } else {
        static const int tile_x[] = {6, 206, 406, 606, 806};
        static const int tile_w[] = {194, 194, 194, 194, 212};
        const uint32_t tile_colors[] = {
            screen->ch1_enabled ? yellow : RGB(96, 101, 112),
            screen->ch2_enabled ? cyan : RGB(96, 101, 112),
            white, trigger_color, RGB(182, 150, 241)
        };
        for (i = 0; i < 5; ++i) {
            int disabled = (i == 0 && !screen->ch1_enabled) ||
                           (i == 1 && !screen->ch2_enabled);
            rounded_fill(tile_x[i], SCOPE_BOTTOM_Y, tile_w[i],
                         SCOPE_BOTTOM_HEIGHT,
                         disabled ? RGB(27, 31, 39) : RGB(8, 10, 19));
            rounded_frame(tile_x[i], SCOPE_BOTTOM_Y, tile_w[i],
                          SCOPE_BOTTOM_HEIGHT,
                          disabled ? RGB(79, 85, 96) : RGB(94, 106, 142));
            fill(tile_x[i] + 1, SCOPE_BOTTOM_Y + 1,
                 tile_w[i] - 2, 3,
                 tile_colors[i]);
        }
        label_chip(6, 54, "CH1", screen->ch1_enabled ? yellow : RGB(79, 85, 96));
        if (screen->ch1_enabled) text(77, 543, screen->ch1_input, 2, white);
        bottom_value(18, screen->ch1_enabled ? screen->ch1_scale : "OFF",
                     screen->ch1_enabled ? yellow : muted);
        mode_icon(152, screen->ch_position_mode[0], 0,
                  screen->ch1_enabled ? yellow : muted);
        label_chip(206, 54, "CH2", screen->ch2_enabled ? cyan : RGB(79, 85, 96));
        if (screen->ch2_enabled) text(277, 543, screen->ch2_input, 2, white);
        bottom_value(218, screen->ch2_enabled ? screen->ch2_scale : "OFF",
                     screen->ch2_enabled ? cyan : muted);
        mode_icon(352, screen->ch_position_mode[1], 0,
                  screen->ch2_enabled ? cyan : muted);
        label_chip(406, 65, "TIME", white);
        bottom_value(418, screen->time_scale, white);
        mode_icon(552, screen->time_position_mode, 1, white);
        label_chip(606, 106, "TRIGGER", trigger_color);
        text(731, 543, screen->trigger_source_channel ? "CH2" : "CH1", 2, white);
        trigger_edge_icon(778, 543, screen->trigger_edge_falling,
                          screen->trigger_edge_both, trigger_color);
        bottom_value(618, screen->trigger_mode, trigger_color);
        label_chip(806, 106, "CURSORS", RGB(182, 150, 241));
        cursor_card_value(818, screen, muted);
        for (i = 0; i < 5; ++i) rounded_done();
    }
    active_rounding = SCOPE_WINDOW_RADIUS;
    if (screen->waveform_loaded && screen->browser_title) {
        int title_width = text_width(screen->browser_title, 2) + 20;
        int title_x = SCOPE_WIDTH - title_width - 12;
        int title_y = screen->cursor_measurement_count ? 97 : SCOPE_PLOT_Y + 7;
        if (title_x < 425) title_x = 425;
        rounded_fill(title_x, title_y, SCOPE_WIDTH - title_x - 12, 25,
                     RGB(31, 38, 64));
        rounded_frame(title_x, title_y, SCOPE_WIDTH - title_x - 12, 25,
                      RGB(105, 119, 173));
        text(title_x + 10, title_y + 6, screen->browser_title, 2, white);
        rounded_done();
    }
    if (screen->status_visible && screen->status_message && screen->status_message[0]) {
        int width = (int)strlen(screen->status_message) * 12 + 24;
        int left;
        int top = screen->cursor_measurement_count ? 97 : SCOPE_PLOT_Y + 3;
        uint32_t background = screen->status_warning ? RGB(83, 25, 34) : RGB(26, 33, 55);
        uint32_t border = screen->status_warning ? RGB(245, 93, 109) : RGB(124, 143, 185);
        uint32_t foreground = screen->status_warning ? RGB(255, 219, 223) : white;
        if (width > SCOPE_WIDTH - 12) width = SCOPE_WIDTH - 12;
        left = (SCOPE_WIDTH - width) / 2;
        draw_alpha = screen->status_alpha ? screen->status_alpha : 255;
        rounded_fill(left, top, width, 25, background);
        rounded_frame(left, top, width, 25, border);
        text(left + 12, top + 6, screen->status_message, 2, foreground);
        rounded_done();
        draw_alpha = 255;
    }
    active_rounding = SCOPE_WINDOW_RADIUS;
    if (screen->measurement_menu) {
        rounded_fill(20, 74, 984, SCOPE_BOTTOM_Y - 84, RGB(17, 19, 31));
        rounded_frame(20, 74, 984, SCOPE_BOTTOM_Y - 84, RGB(105, 119, 173));
        rounded_fill(30, 82, 964, 29, RGB(31, 38, 64));
        text(45, 91, screen->menu_title, 2, white);
        text(45, 121, "CH1", 2, yellow);
        text(537, 121, "CH2", 2, cyan);
        rounded_fill(588, 83, 140, 27, RGB(41, 49, 76));
        rounded_frame(588, 83, 140, 27, RGB(105, 119, 173));
        text(598, 89, screen->measurement_hidden ? "SHOW" : "HIDE", 2, white);
        rounded_done();
        rounded_fill(744, 83, 140, 27,
                     screen->menu_selected == SCOPE_MEASURE_CATALOG_ITEMS ?
                     RGB(83, 36, 47) : RGB(46, 32, 45));
        rounded_frame(744, 83, 140, 27,
                      screen->menu_selected == SCOPE_MEASURE_CATALOG_ITEMS ?
                      RGB(245, 93, 109) : RGB(130, 67, 81));
        text(754, 89, "REMOVE ALL", 2,
             screen->measurement_count ? RGB(255, 143, 155) : muted);
        rounded_done();
        rounded_fill(902, 83, 82, 27, RGB(41, 49, 76));
        rounded_frame(902, 83, 82, 27, RGB(105, 119, 173));
        text(919, 89, "BACK", 2, white);
        rounded_done();
        rounded_done();
        for (i = 0; i < screen->menu_count &&
             i < SCOPE_MEASURE_CATALOG_ITEMS; ++i) {
            int col = i / 8;
            int row = i % 8;
            int x = col ? 522 : 30;
            int y = SCOPE_MEASURE_MENU_ROW_Y + row * SCOPE_MEASURE_MENU_ROW_HEIGHT;
            int selected = i == screen->menu_selected;
            uint32_t accent = selected ? green : RGB(57, 63, 89);
            rounded_fill(x, y, 472, 40,
                         selected ? RGB(43, 51, 82) : RGB(25, 28, 45));
            rounded_frame(x, y, 472, 40,
                          selected ? RGB(113, 130, 172) : accent);
            fill(x, y, 5, 40, accent);
            text(x + 14, y + 12, screen->menu_labels[i], 2, white);
            rounded_fill(x + 344, y + 3, 124, 34, RGB(12, 15, 27));
            rounded_frame(x + 344, y + 3, 124, 34, RGB(61, 76, 100));
            text(x + 355, y + 12, screen->menu_values[i], 2,
                 screen->menu_values[i] && screen->menu_values[i][0] == 'R' ? yellow : green);
            rounded_done();
            rounded_done();
        }
        rounded_done();
        if (screen->measurement_clear_confirm) {
            int left = SCOPE_MEASURE_CONFIRM_X;
            int top = SCOPE_MEASURE_CONFIRM_Y;
            int width = SCOPE_MEASURE_CONFIRM_WIDTH;
            int height = SCOPE_MEASURE_CONFIRM_HEIGHT;
            draw_alpha = 175;
            fill(20, 74, 984, SCOPE_BOTTOM_Y - 84, RGB(5, 7, 13));
            draw_alpha = 255;
            rounded_fill(left, top, width, height, RGB(24, 28, 45));
            rounded_frame(left, top, width, height, RGB(245, 93, 109));
            fill(left + 1, top + 1, width - 2, 5, RGB(245, 93, 109));
            text(left + 22, top + 25, "REMOVE ALL MEASUREMENTS", 2, white);
            for (i = 0; i < 2; ++i) {
                int button_x = left + 16 + i * 200;
                int selected = screen->measurement_clear_choice == i;
                uint32_t color = i ? RGB(245, 93, 109) : green;
                rounded_fill(button_x, top + 88, 188, 44,
                             selected ? RGB(43, 51, 76) : RGB(31, 38, 58));
                rounded_frame(button_x, top + 88, 188, 44,
                              selected ? color : RGB(86, 101, 133));
                text(button_x + (188 - text_width(i ? "REMOVE ALL" : "CANCEL", 2)) / 2,
                     top + 103, i ? "REMOVE ALL" : "CANCEL", 2,
                     selected ? color : white);
                rounded_done();
            }
            rounded_done();
        }
    } else if (screen->menu_open) {
        int menu_x = screen->menu_x;
        int menu_y = screen->menu_y;
        int row_y = menu_y + (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y);
        int control_x = menu_x + (SCOPE_MENU_CONTROL_X - SCOPE_MENU_X);
        int height = SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
                     screen->menu_count * SCOPE_MENU_ROW_HEIGHT + 6;
        int back_x = menu_x + SCOPE_MENU_WIDTH - 104;
        int debug_menu = screen->menu_title &&
                         strncmp(screen->menu_title, "DEBUG", 5) == 0;
        rounded_fill(menu_x, menu_y, SCOPE_MENU_WIDTH, height,
                     RGB(17, 19, 31));
        rounded_frame(menu_x, menu_y, SCOPE_MENU_WIDTH, height,
                      RGB(105, 119, 173));
        rounded_fill(menu_x + 9, menu_y + 8, SCOPE_MENU_WIDTH - 18, 36,
                     RGB(31, 38, 64));
        text(menu_x + 21, menu_y + 15, screen->menu_title, 3,
             debug_menu ? yellow : white);
        if (screen->menu_editing)
            text(back_x - 64, menu_y + 19, "EDIT", 2, yellow);
        else if (screen->menu_page)
            text(back_x - 86, menu_y + 19, screen->menu_page, 2, muted);
        rounded_fill(back_x, menu_y + 10, 90, 31, RGB(41, 49, 76));
        rounded_frame(back_x, menu_y + 10, 90, 31, RGB(105, 119, 173));
        text(back_x + (90 - text_width("BACK", 2)) / 2,
             menu_y + 18, "BACK", 2, white);
        rounded_done();
        rounded_done();
        for (i = 0; i < screen->menu_count && i < SCOPE_MENU_ITEMS; ++i) {
            int row = row_y + i * SCOPE_MENU_ROW_HEIGHT;
            int selected = i == screen->menu_selected;
            int body_x = menu_x + 10;
            int control_w = SCOPE_MENU_CONTROL_WIDTH;
            int debug_item = debug_menu ||
                             (screen->menu_labels[i] &&
                              strcmp(screen->menu_labels[i], "DEBUG") == 0);
            rounded_fill(body_x, row, SCOPE_MENU_WIDTH - 20, 50,
                         selected ? RGB(35, 45, 72) : RGB(25, 28, 45));
            rounded_frame(body_x, row, SCOPE_MENU_WIDTH - 20, 50,
                          selected ? RGB(105, 139, 190) : RGB(57, 63, 89));
            fill(body_x, row, 5, 50,
                 selected ? screen->menu_editing ? yellow : green : RGB(57, 63, 89));
            text(body_x + 16, row + 18, screen->menu_labels[i], 2,
                 debug_item ? yellow : white);
            if (screen->menu_option_count[i] && screen->menu_options[i]) {
                int choice, count = screen->menu_option_count[i];
                if (count > SCOPE_MENU_CHOICES) count = SCOPE_MENU_CHOICES;
                for (choice = 0; choice < count; ++choice) {
                    int left = control_x + choice * control_w / count;
                    int right = control_x + (choice + 1) * control_w / count;
                    int active = choice == screen->menu_option_selected[i];
                    const char *label = screen->menu_options[i][choice];
                    uint32_t background = active ? RGB(33, 117, 196) : RGB(44, 49, 59);
                    rounded_fill(left + 2, row + 7, right - left - 4, 36,
                                 background);
                    rounded_frame(left + 2, row + 7, right - left - 4, 36,
                                  active ? RGB(91, 186, 255) : RGB(71, 78, 91));
                    text(left + (right - left - text_width(label, 2)) / 2,
                         row + 18, label, 2, active ? white : muted);
                    rounded_done();
                }
            } else if (screen->menu_stepper[i]) {
                const char *value = screen->menu_values[i];
                uint32_t value_color = selected && screen->menu_editing ? yellow : white;
                rounded_fill(control_x + 2, row + 7, control_w - 4, 36,
                             RGB(15, 19, 32));
                rounded_frame(control_x + 2, row + 7, control_w - 4, 36,
                              RGB(72, 88, 121));
                fill(control_x + 2, row + 7, 54, 36, RGB(37, 47, 69));
                fill(control_x + control_w - 56, row + 7, 54, 36, RGB(37, 47, 69));
                text(control_x + 20, row + 12, "-", 3, yellow);
                text(control_x + control_w - 39, row + 12, "+", 3, yellow);
                text(control_x + (control_w - text_width(value, 2)) / 2,
                     row + 18, value, 2, value_color);
                rounded_done();
            } else if (screen->menu_values[i] && screen->menu_values[i][0]) {
                const char *value = screen->menu_values[i];
                rounded_fill(control_x + 2, row + 7, control_w - 4, 36,
                             RGB(38, 46, 67));
                rounded_frame(control_x + 2, row + 7, control_w - 4, 36,
                              RGB(72, 88, 121));
                text(control_x + (control_w - text_width(value, 2)) / 2,
                     row + 18, value, 2, green);
                rounded_done();
            }
            rounded_done();
        }
        rounded_done();
        if (screen->browser_delete_confirm) {
            int left = SCOPE_BROWSE_CONFIRM_X;
            int top = SCOPE_BROWSE_CONFIRM_Y;
            int width = SCOPE_BROWSE_CONFIRM_WIDTH;
            int height = SCOPE_BROWSE_CONFIRM_HEIGHT;
            const char *name = screen->menu_labels[screen->menu_selected];
            draw_alpha = 175;
            fill(0, SCOPE_PLOT_Y, SCOPE_WIDTH, SCOPE_BOTTOM_Y - SCOPE_PLOT_Y,
                 RGB(5, 7, 13));
            draw_alpha = 255;
            rounded_fill(left, top, width, height, RGB(24, 28, 45));
            rounded_frame(left, top, width, height, RGB(245, 93, 109));
            fill(left + 1, top + 1, width - 2, 5, RGB(245, 93, 109));
            text(left + 22, top + 20, "DELETE CAPTURE?", 2, white);
            if (name)
                text(left + (width - text_width(name, 2)) / 2,
                     top + 50, name, 2, yellow);
            for (i = 0; i < 2; ++i) {
                int button_x = left + 16 + i * 200;
                int selected = screen->browser_delete_choice == i;
                uint32_t color = i ? RGB(245, 93, 109) : green;
                const char *label = i ? "DELETE" : "CANCEL";
                rounded_fill(button_x, top + 88, 188, 44,
                             selected ? RGB(43, 51, 76) : RGB(31, 38, 58));
                rounded_frame(button_x, top + 88, 188, 44,
                              selected ? color : RGB(86, 101, 133));
                text(button_x + (188 - text_width(label, 2)) / 2,
                     top + 103, label, 2, selected ? color : white);
                rounded_done();
            }
            rounded_done();
        }
    }

    assert(rounded_depth == 0);
}
