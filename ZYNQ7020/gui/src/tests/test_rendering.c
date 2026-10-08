#ifdef NDEBUG
#undef NDEBUG
#endif
#include "demo_signal.h"
#include "rounded_box.h"
#include "boot_splash.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint32_t corners[68 * 68];
static RoundedBox box;
static DemoSignal demo;

static void test_cursor_table_and_fft_overlay(uint32_t *pixels, int stride)
{
    int y, width;
    demo_signal_init(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    scope_screen_cursor_measurement_bounds(&demo.screen, NULL, &y, &width, NULL);
    scope_screen_render(pixels, stride, &demo.screen);
    /* The common frame continues through the boundary between CH1 and CH2. */
    assert(pixels[(y + 33) * stride] == 0x997ec4);
    assert(pixels[(y + 33) * stride + width - 1] == 0x997ec4);
    /* Both channel labels use the same cursor color as the TIME row. */
    {
        int row, x, yy, colored;
        for (row = 0; row < 2; ++row) {
            colored = 0;
            for (yy = y + row * 33 + 9; yy < y + row * 33 + 23; ++yy)
                for (x = 12; x < 54; ++x) {
                    uint32_t pixel = pixels[yy * stride + x];
                    assert(pixel != 0xffc44d && pixel != 0x41c7e2);
                    if (pixel == 0xcfb7ff) colored = 1;
                }
            assert(colored);
        }
    }

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    memset(demo.fft_ch1, 0, sizeof(demo.fft_ch1));
    demo.screen.ch2_enabled = 0;
    scope_screen_render(pixels, stride, &demo.screen);
    /* The FFT baseline reaches the same band as frequency labels. */
    y = SCOPE_PLOT_Y + demo.screen.split_height - 9 - 5;
    assert(pixels[y * stride + 80] == 0xffc44d);
}

static void test_fft_cursor_label(uint32_t *pixels, int stride)
{
    int side, x, y;
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    for (side = 0; side < 2; ++side) {
        int colored = 0;
        int column = side ? 1023 : 100;
        demo_signal_ui_move_fft_cursor(&demo, column);
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = SCOPE_PLOT_Y + 25; y < SCOPE_PLOT_Y + 39; ++y)
            for (x = side ? 800 : 108; x < (side ? 1015 : 280); ++x)
                if (pixels[y * stride + x] == 0xcfb7ff) ++colored;
        assert(colored > 20); /* Label is visible on the available side. */
    }
    demo_signal_ui_move_fft_cursor(&demo, 512);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(SCOPE_PLOT_Y + 22) * stride + 512] == 0xcfb7ff);
    assert(pixels[(SCOPE_PLOT_Y + 1) * stride + 512] == 0xcfb7ff);
    assert(pixels[(SCOPE_PLOT_Y + demo.screen.split_height - 10) * stride + 512] == 0xcfb7ff);
    demo_signal_ui_set_split_height(&demo, 70);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(SCOPE_PLOT_Y + 22) * stride + 512] == 0xcfb7ff);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_move_fft_cursor(&demo, 128);
    y = SCOPE_PLOT_Y + demo.screen.split_height - 14 -
        128 * (demo.screen.split_height - 32) / 255;
    assert(scope_screen_fft_level_at(&demo.screen, y) >= 128);
    assert(scope_screen_fft_level_at(&demo.screen, -1000) == 255);
    assert(scope_screen_fft_level_at(&demo.screen, 1000) == 0);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[y * stride + 201] == 0xcfb7ff);
    assert(pixels[y * stride + 801] == 0xcfb7ff);
    demo_signal_ui_toggle_zoom(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(SCOPE_PLOT_Y + 22) * stride + 512] != 0xcfb7ff);
}

static void test_cursor_card_selection(uint32_t *pixels, int stride)
{
    uint32_t mode_pixels[200 * 28];
    int font, loaded, mode, selected, x, y;
    demo_signal_init(&demo);
    for (font = 0; font < SCOPE_FONT_COUNT; ++font) {
        demo.screen.font_index = (uint8_t)font;
        for (loaded = 0; loaded < 2; ++loaded) {
            demo.screen.waveform_loaded = (uint8_t)loaded;
            for (mode = SCOPE_CURSOR_TIME; mode <= SCOPE_CURSOR_VOLTAGE; ++mode) {
                demo.screen.cursor_mode = (uint8_t)mode;
                for (selected = SCOPE_CURSOR_SELECT_A; selected <= SCOPE_CURSOR_SELECT_FFT; ++selected) {
                    int colored = 0;
                    demo.screen.cursor_selected = (uint8_t)selected;
                    scope_screen_render(pixels, stride, &demo.screen);
                    /* TIME / VERTICAL stays unchanged for all three selections. */
                    for (y = 0; y < 28; ++y)
                        for (x = 0; x < 200; ++x) {
                            uint32_t pixel = pixels[(562 + y) * stride + 814 + x];
                            if (selected == SCOPE_CURSOR_SELECT_A) mode_pixels[y * 200 + x] = pixel;
                            else assert(pixel == mode_pixels[y * 200 + x]);
                        }
                    /* Each selection is shown separately beside the CURSORS heading. */
                    for (y = 543; y < 557; ++y)
                        for (x = 970; x < 1008; ++x)
                            if (pixels[y * stride + x] == 0xb696f1) ++colored;
                    assert(colored > 10);
                }
            }
        }
    }
}

static void test_lowercase_n(uint32_t *pixels, int stride)
{
    uint32_t lowercase[32 * 28];
    int font, x, y, different;
    demo_signal_init(&demo);
    for (font = 0; font < SCOPE_FONT_COUNT; ++font) {
        demo.screen.font_index = (uint8_t)font;
        demo.screen.time_scale = "n";
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = 0; y < 28; ++y)
            for (x = 0; x < 32; ++x)
                lowercase[y * 32 + x] = pixels[(562 + y) * stride + 415 + x];
        demo.screen.time_scale = "N";
        scope_screen_render(pixels, stride, &demo.screen);
        different = 0;
        for (y = 0; y < 28; ++y)
            for (x = 0; x < 32; ++x)
                different |= lowercase[y * 32 + x] != pixels[(562 + y) * stride + 415 + x];
        assert(different);
    }
}

static void test_trigger_icons(uint32_t *pixels, int stride)
{
    uint8_t both[20 * 12];
    int x, y, falling;
    demo_signal_init(&demo);
    demo.screen.trigger_edge_both = 1;
    scope_screen_render(pixels, stride, &demo.screen);
    for (y = 0; y < 12; ++y)
        for (x = 0; x < 20; ++x)
            both[y * 20 + x] = pixels[(545 + y) * stride + 778 + x] == 0xffc44d;
    for (falling = 0; falling < 2; ++falling) {
        demo.screen.trigger_edge_both = 0;
        demo.screen.trigger_edge_falling = (uint8_t)falling;
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = 0; y < 12; ++y)
            for (x = 0; x < 10; ++x) {
                if (x < 5) {
                    assert(pixels[(545 + y) * stride + 778 + x] != 0xffc44d);
                    assert(pixels[(545 + y) * stride + 793 + x] != 0xffc44d);
                }
                assert((pixels[(545 + y) * stride + 783 + x] == 0xffc44d) ==
                       both[y * 20 + falling * 10 + x]);
            }
    }
}

static void test_buttons_and_overlay(uint32_t *pixels, int stride)
{
    uint32_t before;
    demo_signal_init(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0x70dfa3);
    demo_signal_ui_toggle_run(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0xed555f);
    demo.screen.waveform_loaded = 1;
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0xffc44d);

    demo_signal_init(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    before = pixels[550 * stride + 600];
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    demo_signal_ui_move_menu(&demo, 0, 1000);
    scope_screen_render(pixels, stride, &demo.screen);
    /* Last menu row covers the bottom cards when dragged down. */
    assert(pixels[550 * stride + 600] != before);
    assert(pixels[550 * stride + 600] == 0x191c2d);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    assert(strcmp(demo.screen.fft_frequency_labels[0], "1k") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[1], "2.18k") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[8], "512k") == 0);
    assert(strcmp(demo.screen.fft_sampling_info, "FS 1.024M / DF 1k") == 0);
    demo_signal_set_fft_range(&demo, 100000000.0);
    assert(strcmp(demo.screen.fft_frequency_labels[1], "426k") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[8], "100M") == 0);
}

static void test_corner_composition(void)
{
    int i, x, y, partial = 0, edge = 0;
    const uint32_t base = 0x00203040, content = 0x00406080, border = 0x00ffffff;
    for (i = 0; i < 68 * 68; ++i) corners[i] = base;
    rounded_box_begin(&box, corners, 68, 68, 68, 2, 2, 64, 64, 32);
    for (y = 2; y < 66; ++y)
        for (x = 2; x < 66; ++x) corners[y * 68 + x] = content;
    box.border = border;
    box.border_alpha = 255;
    rounded_box_end(&box);
    assert(corners[2 * 68 + 2] == base);
    assert(corners[34 * 68 + 34] == content);
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            uint32_t color = corners[(2 + y) * 68 + 2 + x];
            /* Both lower corners and both sides must have identical coverage. */
            assert(color == corners[(65 - y) * 68 + 2 + x]);
            assert(color == corners[(2 + y) * 68 + 65 - x]);
            partial += color != base && color != content && color != border;
            edge += ((color >> 16) & 255) > 160;
        }
    assert(partial > 20 && edge > 20);
    /* No write outside the widget, even when its corners cross the viewport. */
    assert(corners[0] == base && corners[67 * 68 + 67] == base);
    rounded_box_begin(&box, corners, 68, 68, 68, -10, -10, 64, 64, 32);
    rounded_box_end(&box);
    assert(corners[67 * 68 + 67] == base);
}

int main(void)
{
    static const DemoMenu menus[] = {DEMO_MENU_MAIN, DEMO_MENU_DEBUG, DEMO_MENU_MEASURE,
                                    DEMO_MENU_CH1, DEMO_MENU_PROCESSING};
    uint32_t *pixels = malloc((SCOPE_WIDTH + 3) * SCOPE_HEIGHT * sizeof(*pixels));
    int mode, menu, x, width;
    assert(pixels);
    test_corner_composition();
    test_trigger_icons(pixels, SCOPE_WIDTH + 3);
    test_lowercase_n(pixels, SCOPE_WIDTH + 3);
    test_cursor_table_and_fft_overlay(pixels, SCOPE_WIDTH + 3);
    test_fft_cursor_label(pixels, SCOPE_WIDTH + 3);
    test_cursor_card_selection(pixels, SCOPE_WIDTH + 3);
    test_buttons_and_overlay(pixels, SCOPE_WIDTH + 3);
    boot_splash_render(pixels, SCOPE_WIDTH + 3, SCOPE_WIDTH, SCOPE_HEIGHT);
    assert(pixels[0] == BOOT_SPLASH_BACKGROUND);
    assert(pixels[300 * (SCOPE_WIDTH + 3) + 512] != BOOT_SPLASH_BACKGROUND);
    for (mode = 0; mode < 3; ++mode) {
        demo_signal_init(&demo);
        for (x = 0; x < mode; ++x) demo_signal_ui_cycle_cursor_mode(&demo);
        scope_screen_measurement_bounds(&demo.screen, &x, NULL, &width, NULL);
        assert(x == 0);
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
        for (menu = 0; menu < sizeof(menus) / sizeof(menus[0]); ++menu) {
            demo_signal_ui_open_menu(&demo, menus[menu]);
            scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
            demo_signal_ui_move_menu(&demo, -2000, -2000);
            scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
            demo_signal_ui_dismiss_menu(&demo);
        }
        demo.screen.waveform_loaded = 1;
        demo.screen.browser_title = "TEST CAPTURE";
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
        demo.screen.browser_visible = 1;
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
    }
    scope_screen_render(NULL, 0, NULL);
    free(pixels);
    return 0;
}
