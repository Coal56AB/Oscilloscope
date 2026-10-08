#include "demo_signal.h"
#include "wave_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int first_falling_edge(const int16_t *samples, int threshold)
{
    int x;
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x)
        if (samples[x - 1] < threshold && samples[x] >= threshold) return x;
    return -1;
}

int main(void)
{
    DemoSignal demo;
    static uint32_t pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
    static uint32_t capture_pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
    int16_t synchronized_ch1[SCOPE_PLOT_WIDTH];
    int previous;
    int edge;
    int i;

    demo_signal_init(&demo);
    assert(SCOPE_PLOT_WIDTH == 1024 && SCOPE_PLOT_HEIGHT == 480);
    assert(demo.screen.font_index == SCOPE_FONT_INTER);
    assert(demo.screen.trigger_y == 128);
    assert(demo.screen.channel_zero_y[0] == 200);
    assert(demo.screen.channel_zero_y[1] == 330);
    assert(strcmp(demo.screen.measurement_values[2], "10.00 kHz") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(!demo.screen.menu_open);
    assert(demo.ch1[demo.screen.trigger_marker_x - 1] >
           demo.ch1[demo.screen.trigger_marker_x + 1]);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo.phase = 30;
    demo_signal_advance(&demo);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) != 0);
    assert(demo.screen.trigger_locked);
    assert(first_falling_edge(synchronized_ch1, 150) ==
           first_falling_edge(demo.ch1, 150));
    assert(demo.ch1[demo.screen.trigger_marker_x - 3] >
           demo.ch1[demo.screen.trigger_marker_x + 3]);
    assert(demo.ch1[0] != demo.screen.channel_zero_y[0]);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert(pixels[(SCOPE_PLOT_Y + 200) * SCOPE_WIDTH + 22] == 0x775b1a);
    assert(pixels[(SCOPE_PLOT_Y + 330) * SCOPE_WIDTH + 22] == 0x1b5b6a);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1000);
    assert(demo.screen.ch1_enabled && demo.menu_kind == DEMO_MENU_CH1);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1500);
    assert(!demo.screen.ch1_enabled && !demo.screen.menu_open);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 2500);
    assert(demo.screen.ch1_enabled && !demo.screen.menu_open);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 3500);
    assert(demo.menu_kind == DEMO_MENU_CH1);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 4500);
    assert(!demo.screen.menu_open && demo.screen.ch1_enabled);

    demo_signal_rotate(&demo, DEMO_ENC_CH1, 1);
    assert(strcmp(demo.screen.ch1_scale, "1 V") == 0);
    demo_signal_press(&demo, DEMO_ENC_CH1, 0);
    demo_signal_rotate(&demo, DEMO_ENC_CH1, 1);
    assert(demo.channel_position[0] == -8);
    assert(demo.screen.channel_zero_y[0] == 192);
    demo_signal_press(&demo, DEMO_ENC_CH1, 1);
    assert(demo.channel_position[0] == 40);
    assert(demo.screen.channel_zero_y[0] == 240);
    demo_signal_press(&demo, DEMO_ENC_TIME, 0);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 1);
    assert(demo.time_position == -16);
    assert(demo.screen.trigger_marker_x == SCOPE_PLOT_WIDTH / 2 + 16);
    demo_signal_press(&demo, DEMO_ENC_TIME, 1);
    assert(demo.time_position == 0);

    previous = demo.screen.trigger_y;
    demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, 1);
    assert(demo.screen.trigger_preview && demo.screen.trigger_y == previous);
    assert(demo.screen.trigger_preview_y == previous - 8);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
    assert(!demo.screen.trigger_preview && demo.screen.trigger_y == previous - 8);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 1);
    assert(demo.screen.trigger_y == SCOPE_PLOT_HEIGHT / 2);

    demo_signal_press(&demo, DEMO_BTN_MODE, 1);
    assert(demo.menu_kind == DEMO_MENU_TRIGGER);
    assert(strcmp(demo.screen.menu_labels[0], "MODE") == 0);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(!demo.screen.menu_editing && demo.trigger_source_index == 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.trigger_source_index == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.trigger_source_index == 1);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 1);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.trigger_edge_index == 1);
    assert(strcmp(demo.screen.trigger_source, "CH2 FALL") == 0);
    assert(demo.screen.trigger_edge_falling);
    demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, -5);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
    assert(demo.screen.trigger_locked);
    assert(demo.ch2[demo.screen.trigger_marker_x - 1] <
           demo.ch2[demo.screen.trigger_marker_x + 1]);
    memcpy(synchronized_ch1, demo.ch2, sizeof(synchronized_ch1));
    demo_signal_advance(&demo);
    assert(memcmp(synchronized_ch1, demo.ch2, sizeof(synchronized_ch1)) != 0);
    assert(demo.ch2[demo.screen.trigger_marker_x - 3] <
           demo.ch2[demo.screen.trigger_marker_x + 3]);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(!demo.screen.menu_open);

    demo_signal_press(&demo, DEMO_BTN_MODE, 0);
    assert(strcmp(demo.screen.trigger_mode, "NORMAL") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, 100);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
    previous = demo.phase;
    demo_signal_advance(&demo);
    assert(demo.screen.waiting_for_trigger && demo.phase == previous);
    demo_signal_press(&demo, DEMO_BTN_RUN, 1);
    assert(!demo.screen.waiting_for_trigger && demo.phase != previous);
    demo_signal_press(&demo, DEMO_BTN_MODE, 0);
    assert(strcmp(demo.screen.trigger_mode, "SINGLE") == 0);
    demo_signal_advance(&demo);
    assert(demo.screen.running && demo.screen.waiting_for_trigger);
    demo_signal_press(&demo, DEMO_BTN_RUN, 1);
    assert(!demo.screen.running);

    demo_signal_init(&demo);
    previous = demo.time_index;
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    assert(demo.screen.zoom_enabled && demo.screen.running);
    assert(demo.screen.zoom_window_start == 256);
    assert(demo.screen.zoom_window_end == 768);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo_signal_advance(&demo);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) != 0);
    demo_signal_press(&demo, DEMO_BTN_RUN, 0);
    assert(!demo.screen.running);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo_signal_advance(&demo);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) == 0);
    demo_signal_press(&demo, DEMO_BTN_RUN, 0);
    assert(demo.screen.running);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 2);
    assert(demo.zoom_factor == 4 && demo.time_index == previous);
    assert(demo.screen.zoom_window_start == 384);
    assert(demo.screen.zoom_window_end == 640);
    demo_signal_press(&demo, DEMO_ENC_TIME, 0);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 1);
    assert(demo.zoom_offset == -16 && demo.screen.trigger_y == 128);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    assert(!demo.screen.zoom_enabled && demo.screen.running);
    assert(demo.time_index == previous && demo.time_position == 0);

    demo_signal_press(&demo, DEMO_BTN_CURSOR, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_TIME);
    assert(demo.screen.cursor_measurement_count == 4);
    assert(strcmp(demo.screen.cursor_measurement_labels[0], "A") == 0);
    assert(strcmp(demo.screen.cursor_measurement_labels[1], "B") == 0);
    assert(strcmp(demo.screen.cursor_measurement_labels[2], "DT") == 0);
    assert(strcmp(demo.screen.cursor_measurement_labels[3], "FREQ") == 0);
    assert(scope_screen_cursor_measurement_width(&demo.screen) < SCOPE_WIDTH - 16);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.cursor_a == 248);
    assert(strcmp(demo.screen.cursor_measurement_values[2], "460.9 us") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[3], "2.17 kHz") == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.cursor_selected);
    demo_signal_ui_move_cursor(&demo, demo.screen.cursor_a);
    assert(strcmp(demo.screen.cursor_measurement_values[3], "--") == 0);
    demo_signal_press(&demo, DEMO_BTN_CURSOR, 1);
    assert(demo.menu_kind == DEMO_MENU_CURSOR && demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE);
    assert(demo.screen.menu_count == 2);
    demo_signal_ui_menu_tap(&demo, 1, 0);
    assert(demo.cursor_source_index == 1 && demo.screen.cursor_source_channel == 1);
    assert(strcmp(demo.screen.cursor_measurement_labels[2], "DV") == 0);
    assert(demo.measurement_count == 3);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    demo_signal_press(&demo, DEMO_BTN_CURSOR, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_OFF);
    assert(demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1) == DEMO_ACTION_NONE);
    assert(demo.screen.fine_mode);

    demo_signal_init(&demo);
    demo_signal_press(&demo, DEMO_ENC_CH1, 0);
    previous = demo.channel_position[0];
    demo_signal_rotate(&demo, DEMO_ENC_CH1, 1);
    assert(demo.channel_position[0] == previous - 8);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.fine_mode);
    demo_signal_rotate(&demo, DEMO_ENC_CH1, 1);
    assert(demo.channel_position[0] == previous - 9);
    demo_signal_press(&demo, DEMO_ENC_TIME, 0);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 1);
    assert(demo.time_position == -1);
    previous = demo.screen.trigger_preview_y;
    demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, 1);
    assert(abs(demo.screen.trigger_preview_y - previous) == 1);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.cursor_a == 241);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(!demo.screen.fine_mode);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.fine_mode);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(!demo.screen.menu_open && demo.screen.fine_mode);
    demo_signal_ui_toggle_fine(&demo);
    assert(!demo.screen.fine_mode);

    demo_signal_init(&demo);
    demo_signal_press_at(&demo, DEMO_BTN_CH2, 0, 1000);
    assert(demo.menu_kind == DEMO_MENU_CH2);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(strcmp(demo.screen.ch2_input, "AC   x1") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(strcmp(demo.screen.ch2_input, "AC   x10") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_CH2, 1);
    demo_signal_press(&demo, DEMO_ENC_CH2, 0);
    demo_signal_rotate(&demo, DEMO_ENC_CH2, 1);
    assert(demo.channel_position[1] == -8);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 2);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.calibration_count[1] == 1 && demo.channel_position[1] == -90);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -3);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.coupling[1] == 0 && demo.screen.menu_option_count[1] == 2);
    assert(demo.screen.channel_zero_y[1] == 240);
    assert(demo.ch2[0] != demo.ch2[50]);
    demo_signal_press(&demo, DEMO_ENC_CH2, 1);
    assert(demo.screen.channel_zero_y[1] == 240);

    demo_signal_init(&demo);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1000);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 3);
    assert(strcmp(demo.screen.menu_labels[3], "V / DIV") == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(strcmp(demo.screen.menu_values[3], "1 V") == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.scale_index[0] == 3);
    demo_signal_move_channel(&demo, 0, 20);
    assert(demo.screen.channel_zero_y[0] == 220);
    demo_signal_zoom_channel(&demo, 0, -1);
    assert(demo.scale_index[0] == 2);
    demo_signal_set_trigger_preview_y(&demo, 120);
    assert(demo.screen.trigger_preview && demo.screen.trigger_preview_y == 120);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
    assert(!demo.screen.trigger_preview && demo.screen.trigger_y == 120);
    demo_signal_zoom_time(&demo, 1);
    assert(demo.time_index == 2);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    demo_signal_zoom_time(&demo, 2);
    assert(demo.zoom_factor == 4);
    demo_signal_set_zoom_center(&demo, 600);
    assert(demo.screen.zoom_window_start == 472);
    assert(demo.screen.zoom_window_end == 728);

    demo_signal_init(&demo);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(demo.screen.menu_count == 6);
    assert(strcmp(demo.screen.menu_labels[0], "BROWSE") == 0);
    assert(strcmp(demo.screen.menu_labels[1], "PC CONNECTION") == 0);
    assert(strcmp(demo.screen.menu_labels[3], "MEASUREMENTS") == 0);
    assert(strcmp(demo.screen.menu_labels[4], "PROCESSING") == 0);
    assert(strcmp(demo.screen.menu_labels[5], "DEBUG") == 0);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
    assert(demo.menu_kind == DEMO_MENU_MEASURE);
    assert(demo.screen.measurement_menu && demo.screen.menu_count == 17);
    assert(strcmp(demo.screen.menu_labels[0], "VPP") == 0);
    assert(strcmp(demo.screen.menu_labels[8], "VPP") == 0);
    assert(strcmp(demo.screen.menu_labels[16], "REMOVE ALL") == 0);
    assert(demo.screen.menu_page == NULL);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 8);
    assert(demo.measurement_selected == DEMO_MEAS_CH2_VPP);
    assert(demo.screen.menu_selected == 8);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 2);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -8);
    assert(demo.measurement_selected == DEMO_MEAS_CH1_VPP);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 1);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
    assert(demo.screen.menu_selected == 16);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 1);
    assert(demo.screen.measurement_clear_confirm &&
           !demo.screen.measurement_clear_choice);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 1 && !demo.screen.measurement_clear_confirm);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.measurement_clear_choice);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(demo.measurement_count == 1 && !demo.screen.measurement_clear_confirm);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 0);
    assert(!demo.screen.measurement_clear_confirm);
    assert(strcmp(demo.screen.menu_values[0], "ADD") == 0);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, &edge);
    assert(previous >= 230 && edge == 30);
    demo_signal_ui_menu_select(&demo, 0);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.measurement_count == 1);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(!demo.screen.menu_open);

    demo_signal_init(&demo);
    demo_signal_press(&demo, DEMO_BTN_MODE, 1);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(!demo.screen.menu_editing && demo.trigger_source_index == 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(!demo.screen.menu_open);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_TIME);
    assert(strcmp(demo.screen.menu_title, "TIME SETTINGS") == 0);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.screen.menu_editing);
    demo_signal_ui_menu_adjust(&demo, -1);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.time_index == 2 && !demo.screen.menu_editing);
    demo_signal_ui_pan_time(&demo, 35);
    assert(demo.time_position == -35);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.time_position == 0);
    demo_signal_ui_menu_back(&demo);
    assert(!demo.screen.menu_open);

    demo_signal_ui_open_menu(&demo, DEMO_MENU_CURSOR);
    assert(demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_TIME && demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE && demo.screen.menu_count == 2);
    demo_signal_ui_menu_tap(&demo, 1, 0);
    assert(demo.cursor_source_index == 1);
    demo_signal_ui_menu_back(&demo);
    assert(!demo.screen.menu_open);

    demo.screen.ch2_enabled = 0;
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert(pixels[550 * SCOPE_WIDTH + 300] == 0x1b1f27);
    assert(pixels[(SCOPE_BOTTOM_Y + 2) * SCOPE_WIDTH + 240] == 0x606570);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_CH1);
    assert(strcmp(demo.screen.menu_labels[0], "CHANNEL") == 0);
    assert(strcmp(demo.screen.menu_labels[4], "CALIBRATE") == 0);
    demo_signal_ui_menu_tap(&demo, 1, 0);
    assert(demo.coupling[0] == 1 && !demo.screen.menu_editing);
    demo_signal_ui_menu_tap(&demo, 2, 0);
    assert(demo.probe_ten[0] == 1 && !demo.screen.menu_editing);
    demo_signal_ui_menu_tap(&demo, 3, 1);
    assert(demo.scale_index[0] == 3);
    demo_signal_ui_menu_tap(&demo, 3, -1);
    assert(demo.scale_index[0] == 2);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(!demo.screen.ch1_enabled && !demo.screen.menu_editing);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_TIME);
    demo_signal_ui_menu_tap(&demo, 0, 1);
    assert(demo.time_index == 4 && !demo.screen.menu_editing);
    demo_signal_ui_menu_tap(&demo, 0, -1);
    assert(demo.time_index == 3);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_TRIGGER);
    assert(demo.screen.menu_option_count[0] == 3);
    assert(demo.screen.menu_option_count[1] == 2);
    assert(demo.screen.menu_option_count[3] == 4);
    assert(demo.screen.menu_stepper[6]);
    previous = demo.screen.trigger_y;
    demo_signal_ui_menu_choose(&demo, 1, 1);
    assert(demo.trigger_source_index == 1 && !demo.screen.menu_editing);
    assert(demo.screen.trigger_y == previous);
    demo_signal_ui_menu_choose(&demo, 0, 2);
    assert(demo.trigger_mode_index == 2 && !demo.screen.menu_editing);
    demo_signal_ui_menu_choose(&demo, 0, 0);
    demo_signal_ui_menu_tap(&demo, 2, 0);
    assert(demo.trigger_edge_index == 1 && !demo.screen.menu_editing);
    demo_signal_ui_menu_select(&demo, 6);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.screen.trigger_preview && demo.screen.menu_editing);
    previous = demo.screen.trigger_y;
    demo_signal_ui_menu_adjust(&demo, 1);
    assert(demo.screen.trigger_preview_y == previous - 8);
    demo_signal_ui_menu_back(&demo);
    assert(!demo.screen.trigger_preview && demo.screen.trigger_y == previous);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_adjust(&demo, 1);
    demo_signal_ui_menu_activate(&demo);
    assert(!demo.screen.trigger_preview && demo.screen.trigger_y == previous - 8);

    demo_signal_init(&demo);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    assert(demo.screen.menu_count == 5);
    assert(strcmp(demo.screen.menu_labels[0], "CH1 FILTER") == 0);
    assert(demo.screen.menu_option_count[0] == 4);
    demo_signal_ui_menu_choose(&demo, 0, 1);
    assert(demo.filter_level[0] == 1 && !demo.screen.menu_editing);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) != 0);
    demo_signal_ui_menu_choose(&demo, 2, 1);
    assert(demo.screen.intensity_coloring);
    demo_signal_ui_menu_tap(&demo, 3, 0);
    demo_signal_ui_menu_tap(&demo, 3, 0);
    assert(demo.persistence_index == 2);
    for (i = 0; i < 9; ++i) demo_signal_advance(&demo);
    assert(demo.screen.history_count == 8);
    {
        static uint32_t plain[SCOPE_WIDTH * SCOPE_HEIGHT];
        demo.screen.intensity_coloring = 0;
        scope_screen_render(plain, SCOPE_WIDTH, &demo.screen);
        demo.screen.intensity_coloring = 1;
        scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
        assert(memcmp(plain, pixels, sizeof(plain)) != 0);
    }
    demo_signal_ui_dismiss_menu(&demo);
    assert(!demo.screen.menu_open);

    {
        ScopeScreen screen = {0};
        screen.trigger_y = 50;
        screen.trigger_preview_y = 80;
        screen.trigger_preview = 1;
        screen.trigger_source_channel = 1;
        scope_screen_render(pixels, SCOPE_WIDTH, &screen);
        assert(pixels[(SCOPE_PLOT_Y + 50) * SCOPE_WIDTH + SCOPE_PLOT_X + 100] == 0x52d2eb);
        assert(pixels[(SCOPE_PLOT_Y + 80) * SCOPE_WIDTH + SCOPE_PLOT_X + 100] == 0x52d2eb);
        assert(pixels[(SCOPE_PLOT_Y + 80) * SCOPE_WIDTH + SCOPE_PLOT_X + 105] != 0x52d2eb);
        assert(pixels[(SCOPE_PLOT_Y + 58) * SCOPE_WIDTH + SCOPE_WIDTH - 2] == 0x52d2eb);
        assert(pixels[(SCOPE_PLOT_Y + 58) * SCOPE_WIDTH + 2] != 0x52d2eb);
    }

    {
        ScopeScreen screen = {0};
        static int16_t history[8][SCOPE_PLOT_WIDTH];
        int frame, x;
        uint32_t common, rare;
        for (frame = 0; frame < 8; ++frame)
            for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
                history[frame][x] = frame == 7 ? 220 : 200;
        screen.ch1_enabled = 1;
        screen.intensity_coloring = 1;
        screen.history_count = 8;
        screen.history_ch1_samples = &history[0][0];
        scope_screen_render(pixels, SCOPE_WIDTH, &screen);
        common = pixels[(SCOPE_PLOT_Y + 200) * SCOPE_WIDTH + 300];
        rare = pixels[(SCOPE_PLOT_Y + 220) * SCOPE_WIDTH + 300];
        assert(((common >> 16) & 255) > (common & 255));
        assert((rare & 255) > ((rare >> 16) & 255));
    }

    demo_signal_init(&demo);
    demo_signal_advance(&demo);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo_signal_press(&demo, DEMO_BTN_RUN, 0);
    assert(!demo.screen.running);
    demo_signal_advance(&demo);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) == 0);

    demo_signal_init(&demo);
    demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, 100);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
    edge = first_falling_edge(demo.ch1, 200);
    for (i = 0; i < 10; ++i) demo_signal_advance(&demo);
    assert(first_falling_edge(demo.ch1, 200) != edge);
    assert(!demo.screen.trigger_locked);
    assert(!demo.screen.waiting_for_trigger);
    demo_signal_init(&demo);
    demo_signal_press(&demo, DEMO_BTN_MODE, 0);
    demo_signal_press(&demo, DEMO_BTN_MODE, 0);
    demo_signal_advance(&demo);
    assert(!demo.screen.running && !demo.screen.waiting_for_trigger);

    demo_signal_init(&demo);
    assert(!demo.screen.status_visible);
    demo_signal_zoom_time(&demo, 1);
    assert(!demo.screen.status_visible);
    demo_signal_notify(&demo, "SCREENSHOT SAVED");
    assert(demo.screen.status_visible);
    assert(!demo.screen.status_warning && demo.screen.status_alpha == 255);
    for (i = 0; i < 45; ++i) demo_signal_advance(&demo);
    assert(!demo.screen.status_visible);
    demo_signal_notify(&demo, "MEASUREMENT LIST FULL / REMOVE ONE");
    assert(demo.screen.status_warning && demo.screen.status_alpha == 255);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    previous = (int)pixels[65 * SCOPE_WIDTH + 300];
    assert((previous >> 16 & 255) > (previous >> 8 & 255));
    for (i = 0; i < 35; ++i) demo_signal_advance(&demo);
    assert(demo.screen.status_visible && demo.screen.status_alpha < 255);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert((int)pixels[65 * SCOPE_WIDTH + 300] != previous);
    for (i = 0; i < 10; ++i) demo_signal_advance(&demo);
    assert(!demo.screen.status_visible);
    assert(demo.screen.measurement_horizontal);
    assert(demo.screen.measurement_x == 8 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 62);
    demo_signal_ui_move_measurements(&demo, 100, -80);
    assert(demo.screen.measurement_x == 108 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_ui_toggle_measurement_layout(&demo);
    assert(!demo.screen.measurement_horizontal);
    assert(demo.screen.measurement_x == 108);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, NULL);
    assert(previous < 984);
    demo_signal_ui_toggle_measurements_visible(&demo);
    assert(demo.screen.measurement_hidden && demo.measurement_count == 3);
    scope_screen_measurement_bounds(&demo.screen, &i, &edge, NULL, NULL);
    assert(i == 0 && edge == SCOPE_MEASURE_BOTTOM_Y - 30);
    assert(demo.screen.measurement_x == 108 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_ui_move_measurements(&demo, 300, 100);
    assert(demo.screen.measurement_x == 108 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_advance(&demo);
    assert(demo.screen.measurement_hidden && demo.measurement_count == 3);
    demo_signal_ui_toggle_measurements_visible(&demo);
    assert(!demo.screen.measurement_hidden && demo.measurement_count == 3);
    assert(demo.screen.measurement_x == 108 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &edge, NULL);
    assert(edge == previous);
    demo_signal_ui_move_measurements(&demo, 3000, 3000);
    assert(demo.screen.measurement_x == SCOPE_WIDTH - previous);
    assert(demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 126);
    demo_signal_ui_toggle_measurement_layout(&demo);
    scope_screen_measurement_bounds(&demo.screen, NULL, &i, NULL, &edge);
    assert(demo.screen.measurement_horizontal &&
           i + edge == SCOPE_MEASURE_BOTTOM_Y);
    demo_signal_ui_toggle_measurement_layout(&demo);
    scope_screen_measurement_bounds(&demo.screen, NULL, &i, NULL, &edge);
    assert(!demo.screen.measurement_horizontal &&
           i + edge == SCOPE_MEASURE_BOTTOM_Y);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_TIME);
    assert(demo.screen.cursor_measurement_count == 4);
    assert(strcmp(demo.screen.cursor_measurement_labels[2], "DT") == 0);
    scope_screen_measurement_bounds(&demo.screen, NULL, &i, NULL, &edge);
    assert(i + edge == SCOPE_CURSOR_STRIP_Y);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE);
    assert(demo.screen.cursor_measurement_count == 3);
    assert(strcmp(demo.screen.cursor_measurement_labels[2], "DV") == 0);
    scope_screen_measurement_bounds(&demo.screen, NULL, &i, NULL, &edge);
    assert(i + edge == SCOPE_CURSOR_STRIP_Y);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_OFF);
    assert(demo.screen.cursor_measurement_count == 0);
    scope_screen_measurement_bounds(&demo.screen, NULL, &i, NULL, &edge);
    assert(i + edge == SCOPE_MEASURE_BOTTOM_Y);
    demo_signal_ui_cycle_trigger_mode(&demo);
    assert(demo.trigger_mode_index == 1);
    demo_signal_ui_move_measurements(&demo, -3000, -3000);
    assert(demo.screen.measurement_y == SCOPE_PLOT_Y);
    demo_signal_move_channel(&demo, 1, 40);
    demo_signal_ui_reset_channel_position(&demo, 1);
    assert(demo.screen.channel_zero_y[1] == SCOPE_PLOT_HEIGHT / 2);
    demo_signal_ui_pan_time(&demo, 24);
    demo_signal_ui_reset_time_position(&demo);
    assert(demo.time_position == 0);

    demo_signal_init(&demo);
    assert(demo.screen.measurement_horizontal);
    demo_signal_ui_move_measurements(&demo, 3000, 0);
    assert(demo.screen.measurement_x > 8);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_select(&demo, 2);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.measurement_count == 5 && demo.screen.measurement_x == 40);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, NULL);
    assert(previous == 984);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
    {
        static const int additions[] = {1, 2, 3, 4, 6, 7, 9};
        for (i = 0; i < 7; ++i) {
            demo_signal_ui_menu_select(&demo, additions[i]);
            demo_signal_ui_menu_activate(&demo);
        }
    }
    assert(demo.measurement_count == 10);
    demo_signal_ui_menu_select(&demo, 15);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.measurement_count == 10);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, &edge);
    assert(previous == 984 && edge == 94);
    demo_signal_ui_toggle_measurement_layout(&demo);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, &edge);
    assert(previous < 600 && edge == 190);
    {
        int compact_width = previous;
        const char *saved_value = demo.screen.measurement_values[7];
        demo.screen.measurement_values[7] = "123456789 V";
        scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, NULL);
        assert(previous > compact_width);
        demo.screen.measurement_values[7] = saved_value;
    }
    {
        ScopeScreen compact = {0};
        int prefixed_width;
        compact.measurement_count = SCOPE_MEASURE_SLOTS;
        for (i = 0; i < SCOPE_MEASURE_SLOTS; ++i) {
            compact.measurement_labels[i] = "CH1 PERIOD";
            compact.measurement_values[i] = "100.0 us";
        }
        scope_screen_measurement_bounds(&compact, NULL, NULL, &prefixed_width, NULL);
        for (i = 0; i < SCOPE_MEASURE_SLOTS; ++i)
            compact.measurement_labels[i] = "PERIOD";
        scope_screen_measurement_bounds(&compact, NULL, NULL, &previous, NULL);
        assert(prefixed_width == previous && previous == 392);
    }

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_select(&demo, 4);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.fft_enabled && !demo.screen.menu_editing);
    assert(demo.screen.fft_ch1_bins[10] > demo.screen.fft_ch1_bins[9]);
    demo_signal_ui_set_split_height(&demo, 170);
    assert(demo.screen.split_height == 170);
    demo_signal_ui_toggle_zoom(&demo);
    assert(demo.screen.zoom_enabled && !demo.screen.fft_enabled);
    demo_signal_ui_set_split_height(&demo, 180);
    assert(demo.screen.split_height == 180);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DISPLAY);
    assert(demo.screen.menu_count == 1);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DEBUG);
    assert(demo.screen.menu_count == 3);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_FONT);
    assert(demo.screen.menu_count == SCOPE_FONT_COUNT);
    for (i = 0; i < SCOPE_FONT_COUNT; ++i) {
        demo_signal_ui_menu_select(&demo, i);
        demo_signal_ui_menu_activate(&demo);
        assert(demo.screen.font_index == i);
        assert(strcmp(demo.screen.menu_values[i], "ACTIVE") == 0);
        scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    }
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    assert(demo.screen.menu_option_count[2] == 4);
    demo_signal_ui_menu_choose(&demo, 2, 3);
    assert(demo.screen.ui_rounding == 3);
    assert(strcmp(demo.screen.menu_values[2], "8 PX") == 0);
    demo_signal_ui_dismiss_menu(&demo);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    previous = (int)pixels[SCOPE_BOTTOM_Y * SCOPE_WIDTH + 6];
    demo.screen.ui_rounding = 0;
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert(previous != (int)pixels[SCOPE_BOTTOM_Y * SCOPE_WIDTH + 6]);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    assert(demo.screen.menu_count == 6);
    assert(demo.screen.menu_x == SCOPE_MENU_X && demo.screen.menu_y == SCOPE_MENU_Y);
    demo_signal_ui_move_menu(&demo, -1000, -1000);
    assert(demo.screen.menu_x == 0 && demo.screen.menu_y == 0);
    demo_signal_ui_move_menu(&demo, 1000, 1000);
    assert(demo.screen.menu_x == SCOPE_WIDTH - SCOPE_MENU_WIDTH);
    assert(demo.screen.menu_y == SCOPE_HEIGHT -
           (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
            demo.screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6));
    previous = demo.screen.menu_x;
    edge = demo.screen.menu_y;
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    assert(demo.screen.menu_x == previous && demo.screen.menu_y == edge);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 3);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_MEASURE && demo.menu_parent == DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    assert(demo.screen.menu_x == previous && demo.screen.menu_y == edge);

    demo_signal_ui_menu_select(&demo, 5);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind == DEMO_MENU_GENERATOR);
    assert(demo.screen.menu_count == 3);
    assert(strcmp(demo.screen.menu_values[0], "SQUARE") == 0);
    memcpy(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1));
    demo_signal_ui_menu_tap(&demo, 0, 1);
    assert(demo.generator_wave[0] == DEMO_WAVE_SINE);
    demo_signal_ui_menu_tap(&demo, 0, 1);
    demo_signal_ui_menu_tap(&demo, 0, 1);
    demo_signal_ui_menu_tap(&demo, 0, 1);
    assert(demo.generator_wave[0] == DEMO_WAVE_SINC);
    assert(strcmp(demo.screen.menu_values[0], "SINC") == 0);
    assert(memcmp(synchronized_ch1, demo.ch1, sizeof(synchronized_ch1)) != 0);
    demo_signal_ui_menu_select(&demo, 2);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.generator_frequency_index == 4);
    assert(strcmp(demo.screen.menu_values[2], "20 kHz") == 0);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 4);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_PROCESSING && demo.menu_parent == DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.menu_kind == DEMO_MENU_MAIN);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_NONE);
    assert(demo.menu_kind == DEMO_MENU_BROWSE_TYPE);
    assert(strcmp(demo.screen.menu_labels[0], "BMP IMAGES") == 0);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_BROWSE_REFRESH);
    assert(demo.menu_kind == DEMO_MENU_BROWSE);
    assert(demo.browse_filter == 0);
    {
        static const char *files[] = {
            "capture_0010.bmp", "capture_0009.bmp", "capture_0008.bmp",
            "capture_0007.bmp", "capture_0006.bmp", "capture_0005.bmp",
            "capture_0004.bmp", "capture_0003.bmp", "capture_0002.bmp",
            "capture_0001.bmp"
        };
        demo_signal_ui_set_browse_files(&demo, files, 10);
    }
    assert(strcmp(demo.screen.menu_labels[0], "CAPTURE_0010.BMP") == 0);
    demo_signal_ui_menu_adjust(&demo, 8);
    assert(demo.browse_selected == 8 && demo.screen.menu_count == 3);
    assert(strcmp(demo_signal_ui_browse_filename(&demo), "capture_0002.bmp") == 0);
    demo_signal_ui_request_browse_delete(&demo, demo.screen.menu_selected);
    assert(demo.screen.browser_delete_confirm &&
           !demo.screen.browser_delete_choice);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert(pixels[SCOPE_BROWSE_CONFIRM_Y * SCOPE_WIDTH +
                  SCOPE_BROWSE_CONFIRM_X] == 0xf55d6d);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_NONE);
    assert(!demo.screen.browser_delete_confirm);
    demo_signal_ui_request_browse_delete(&demo, demo.screen.menu_selected);
    demo_signal_ui_menu_adjust(&demo, 1);
    assert(demo.screen.browser_delete_choice);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_BROWSE_DELETE);
    assert(!demo.screen.browser_delete_confirm);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_BROWSE_OPEN);
    capture_pixels[100 * SCOPE_WIDTH + 100] = 0x123456;
    demo_signal_ui_show_capture(&demo, demo_signal_ui_browse_filename(&demo),
                                capture_pixels);
    assert(demo.screen.browser_visible && !demo.screen.menu_open);
    assert(strcmp(demo.screen.browser_title, "CAPTURE_0002.BMP") == 0);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert(pixels[100 * SCOPE_WIDTH + 100] == 0x123456);
    demo_signal_ui_close_capture(&demo);
    assert(!demo.screen.browser_visible && demo.menu_kind == DEMO_MENU_BROWSE);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_BROWSE_TYPE);
    demo_signal_ui_menu_select(&demo, 1);
    assert(demo_signal_ui_menu_activate(&demo) == DEMO_ACTION_BROWSE_REFRESH);
    assert(demo.browse_filter == 1 && demo.menu_kind == DEMO_MENU_BROWSE);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_BROWSE_TYPE);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_PC);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.pc_mode == 1 && !demo.screen.menu_editing);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
    demo_signal_ui_menu_select(&demo, 6);
    demo_signal_ui_menu_activate(&demo);
    assert(strcmp(demo.screen.measurement_values[3], "100.0 us") == 0);
    demo_signal_ui_menu_select(&demo, 7);
    demo_signal_ui_menu_activate(&demo);
    assert(strcmp(demo.screen.measurement_values[4], "50.0 %") == 0);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_TRIGGER);
    assert(demo.screen.menu_option_count[2] == 3);
    demo_signal_ui_menu_choose(&demo, 2, 2);
    assert(demo.trigger_edge_index == 2 && demo.screen.trigger_edge_both);
    demo_signal_ui_dismiss_menu(&demo);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    for (i = 0; i < 4; ++i) {
        assert(pixels[555 * SCOPE_WIDTH + 778 + i] == 0xffc44d);
        assert(pixels[555 * SCOPE_WIDTH + 797 - i] == 0xffc44d);
    }
    for (i = 0; i < 8; ++i)
        assert(pixels[545 * SCOPE_WIDTH + 784 + i] == 0xffc44d);
    assert(pixels[548 * SCOPE_WIDTH + 782] == 0xffc44d);
    assert(pixels[551 * SCOPE_WIDTH + 779] == 0xffc44d);
    assert(pixels[548 * SCOPE_WIDTH + 788] == 0xffc44d);
    assert(pixels[551 * SCOPE_WIDTH + 791] == 0xffc44d);
    assert(demo.ch1[demo.screen.trigger_marker_x - 3] >
           demo.ch1[demo.screen.trigger_marker_x + 3]);
    demo_signal_advance(&demo);
    assert(demo.ch1[demo.screen.trigger_marker_x - 3] <
           demo.ch1[demo.screen.trigger_marker_x + 3]);

    {
        DemoWaveCapture wave;
        DemoWaveCapture restored;
        FILE *file;
        int16_t original[SCOPE_PLOT_WIDTH];
        demo_signal_init(&demo);
        {
            static const char *csv_files[] = {"capture_0001.csv"};
            demo.browse_filter = 1;
            demo_signal_ui_set_browse_files(&demo, csv_files, 1);
        }
        demo_signal_export_wave(&demo, &wave);
        file = tmpfile();
        assert(file);
        assert(wave_file_write(file, &wave));
        rewind(file);
        memset(&restored, 0, sizeof(restored));
        assert(wave_file_read(file, &restored));
        assert(restored.time_us_per_div == wave.time_us_per_div);
        assert(restored.zero_y[0] == wave.zero_y[0]);
        assert(restored.scale_index[1] == wave.scale_index[1]);
        assert(memcmp(restored.ch1, wave.ch1, sizeof(wave.ch1)) == 0);
        assert(memcmp(restored.ch2, wave.ch2, sizeof(wave.ch2)) == 0);
        fclose(file);
        file = tmpfile();
        assert(file);
        assert(fputs("sample,ch1_y,ch2_y\n", file) >= 0);
        for (i = 0; i < SCOPE_PLOT_WIDTH; ++i)
            assert(fprintf(file, "%d,%d,%d\n", i, wave.ch1[i], wave.ch2[i]) > 0);
        rewind(file);
        restored = wave;
        memset(restored.ch1, 0, sizeof(restored.ch1));
        assert(wave_file_read(file, &restored));
        assert(memcmp(restored.ch1, wave.ch1, sizeof(wave.ch1)) == 0);
        fclose(file);
        memcpy(original, wave.ch1, sizeof(original));
        demo_signal_ui_show_wave(&demo, "capture_0001.csv", &wave);
        assert(demo.waveform_loaded && demo.screen.waveform_loaded);
        assert(!demo.screen.running && !demo.screen.browser_visible);
        scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
        assert(pixels[3 * SCOPE_WIDTH + 900] == 0xffc44d);
        assert(pixels[(SCOPE_BOTTOM_Y + 1) * SCOPE_WIDTH + 307] == 0xffc44d);
        assert(pixels[(SCOPE_BOTTOM_Y + 1) * SCOPE_WIDTH + 841] == 0xb696f1);
        assert(pixels[546 * SCOPE_WIDTH + 12] == 0x6977ad);
        demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
        assert(demo.screen.menu_count == 5);
        assert(strcmp(demo.screen.menu_labels[0], "PC CONNECTION") == 0);
        demo_signal_ui_menu_activate(&demo);
        assert(demo.menu_kind == DEMO_MENU_PC);
        demo_signal_ui_menu_back(&demo);
        assert(demo.menu_kind == DEMO_MENU_MAIN);
        demo_signal_ui_dismiss_menu(&demo);
        assert(memcmp(demo.ch1, original, sizeof(original)) == 0);
        demo_signal_zoom_time(&demo, 1);
        assert(demo.time_index == 2 &&
               memcmp(demo.ch1, original, sizeof(original)) != 0);
        memcpy(original, demo.ch1, sizeof(original));
        previous = demo.screen.zoom_window_start;
        edge = demo.screen.trigger_marker_x;
        for (i = 0; i < 80; ++i) demo_signal_ui_pan_time(&demo, 1);
        assert(previous - demo.screen.zoom_window_start == 40);
        assert(demo.screen.trigger_marker_x - edge == 80);
        assert(memcmp(demo.ch1, original, sizeof(original)) != 0);
        demo_signal_move_channel(&demo, 0, 20);
        assert(demo.screen.channel_zero_y[0] == wave.zero_y[0] + 20);
        demo_signal_zoom_channel(&demo, 0, 1);
        assert(demo.scale_index[0] == wave.scale_index[0] + 1);
        assert(memcmp(demo.loaded_wave.ch1, wave.ch1, sizeof(wave.ch1)) == 0);
        demo_signal_ui_toggle_zoom(&demo);
        assert(demo.screen.zoom_enabled && !demo.screen.running);
        demo_signal_set_zoom_center(&demo, SCOPE_PLOT_WIDTH / 2);
        previous = demo.screen.zoom_window_start;
        edge = demo.screen.trigger_marker_x;
        for (i = 0; i < 80; ++i) demo_signal_ui_pan_time(&demo, 1);
        assert(previous - demo.screen.zoom_window_start == 20);
        assert(demo.screen.trigger_marker_x - edge == 80);
        demo_signal_set_zoom_center(&demo, 700);
        assert(demo.screen.zoom_window_start > 0);
        demo_signal_ui_cycle_cursor_mode(&demo);
        assert(demo.screen.cursor_measurement_count == 4);
        assert(demo_signal_press(&demo, DEMO_BTN_RUN, 0) ==
               DEMO_ACTION_BROWSE_CLOSE);
        demo_signal_ui_close_capture(&demo);
        assert(!demo.waveform_loaded && !demo.screen.waveform_loaded);
        assert(demo.menu_kind == DEMO_MENU_BROWSE && demo.screen.menu_open);
        assert(demo.browse_filter == 1);
    }
    return 0;
}
