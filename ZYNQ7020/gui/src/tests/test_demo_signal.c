#ifdef NDEBUG
#undef NDEBUG
#endif
#include "demo_signal.h"
#include "spectrum_bins.h"
#include "capture_settings.h"
#include "battery_level.h"
#include "wave_file.h"
#include "waveform_view.h"

#include <assert.h>
#include <math.h>
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

static void test_default_double_click(void)
{
    static DemoSignal demo;
    int channel, interval;
    for (channel = 0; channel < 2; ++channel) {
        for (interval = 300; interval <= 301; ++interval) {
            DemoControl button = channel ? DEMO_BTN_CH2 : DEMO_BTN_CH1;
            demo_signal_init(&demo);
            assert(demo.double_click_ms == 300);
            demo_signal_press_at(&demo, button, 0, 1000);
            demo_signal_press_at(&demo, button, 0, (uint64_t)(1000 + interval));
            assert((channel ? demo.screen.ch2_enabled : demo.screen.ch1_enabled) == (interval > 300));
            assert(channel ? demo.screen.ch1_enabled : demo.screen.ch2_enabled);
            assert(demo.screen.cursor_mode == SCOPE_CURSOR_OFF && demo.trigger_mode_index == 0);
        }
    }
}

static void test_menu_button_holds(void)
{
    static DemoSignal demo;
    static const DemoControl buttons[] = {
        DEMO_BTN_CH1, DEMO_BTN_CH2, DEMO_BTN_CURSOR, DEMO_BTN_MODE, DEMO_BTN_MENU
    };
    static const DemoMenu menus[] = {
        DEMO_MENU_CH1, DEMO_MENU_CH2, DEMO_MENU_CURSOR, DEMO_MENU_TRIGGER, DEMO_MENU_MAIN
    };
    unsigned i;
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        demo_signal_init(&demo);
        demo_signal_ui_open_menu(&demo, menus[i]);
        demo_signal_press_at(&demo, buttons[i], 1, 1000);
        assert(!demo.screen.menu_open && demo.menu_kind == DEMO_MENU_NONE);
        assert(demo.screen.ch1_enabled && demo.screen.ch2_enabled);
        assert(demo.screen.cursor_mode == SCOPE_CURSOR_OFF);
        assert(!demo.screen.zoom_enabled && demo.trigger_mode_index == 0);
        if (buttons[i] != DEMO_BTN_MENU) {
            demo_signal_press_at(&demo, buttons[i], 1, 2000);
            assert(demo.menu_kind == menus[i]);
        }
    }
    demo_signal_init(&demo);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1000);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 1, 1300);
    assert(!demo.screen.menu_open && demo.screen.ch1_enabled);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1400);
    assert(demo.menu_kind == DEMO_MENU_CH1 && demo.screen.ch1_enabled);
    demo_signal_ui_dismiss_menu(&demo);
    demo.screen.ch1_enabled = 0;
    demo_signal_press(&demo, DEMO_BTN_CH1, 1);
    assert(demo.menu_kind == DEMO_MENU_CH1 && !demo.screen.ch1_enabled);
    demo_signal_press(&demo, DEMO_BTN_CH1, 1);
    assert(!demo.screen.menu_open && !demo.screen.ch1_enabled);

    demo_signal_ui_open_menu(&demo, DEMO_MENU_TRIGGER);
    demo_signal_ui_menu_select(&demo, 6);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_adjust(&demo, 1);
    assert(demo.screen.trigger_preview);
    demo_signal_press(&demo, DEMO_BTN_MODE, 1);
    assert(!demo.screen.menu_open && !demo.screen.trigger_preview);
    assert(demo.trigger_voltage == 0.60);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DEBUG);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    assert(!demo.screen.menu_open && !demo.screen.zoom_enabled);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    assert(demo.screen.zoom_enabled);
}

static void test_scale_direction_and_stopped_trigger(void)
{
    static DemoSignal demo;
    static double source[2][DEMO_CAPTURE_POINTS];
    int16_t ch1[SCOPE_PLOT_WIDTH], ch2[SCOPE_PLOT_WIDTH];
    int16_t minimum[2][SCOPE_PLOT_WIDTH], maximum[2][SCOPE_PLOT_WIDTH];
    int channel, zoom;
    double start;
    uint32_t sequence;
    demo_signal_init(&demo);
    for (channel = 0; channel < 2; ++channel) {
        int before = demo.scale_index[channel];
        demo_signal_rotate(&demo, channel ? DEMO_ENC_CH2 : DEMO_ENC_CH1, 1);
        assert(demo.scale_index[channel] == before - 1);
        demo_signal_rotate(&demo, channel ? DEMO_ENC_CH2 : DEMO_ENC_CH1, -1);
        assert(demo.scale_index[channel] == before);
    }
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DEBUG);
    assert(demo.screen.menu_count == 6);
    assert(demo.screen.menu_option_count[3] == 2 && !demo.screen.menu_stepper[3]);
    int previous = demo.adc_simulation;
    demo_signal_ui_menu_select(&demo, 3);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.adc_simulation == !previous && !demo.screen.menu_editing);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.adc_simulation == previous && !demo.screen.menu_editing);
    assert(strcmp(demo.screen.menu_labels[1], "LONG PRESS") == 0);
    assert(strcmp(demo.screen.menu_labels[2], "DOUBLE CLICK") == 0);
    assert(demo.hold_time_ms == 300 && demo.double_click_ms == 300);
    demo_signal_ui_menu_tap(&demo, 1, 1);
    assert(demo.hold_time_ms == 350);
    demo_signal_ui_menu_tap(&demo, 2, -1);
    assert(demo.double_click_ms == 250);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_adjust(&demo, 1);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.double_click_ms == 300);
    demo_signal_ui_dismiss_menu(&demo);
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 1, 0));
    assert(demo.hold_time_ms == 200);
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 1, 26));
    assert(demo.hold_time_ms == 1500);
    assert(!demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 1, 27));
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 2, 0));
    assert(demo.double_click_ms == 100);
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 2, 28));
    assert(demo.double_click_ms == 1500);
    assert(!demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 2, 29));
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 2, 4));
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1000);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1301);
    assert(demo.screen.ch1_enabled);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1601);
    assert(!demo.screen.ch1_enabled);

    for (zoom = 0; zoom < 2; ++zoom) {
        demo_signal_init(&demo);
        demo_signal_advance(&demo);
        memcpy(ch1, demo.ch1, sizeof(ch1));
        demo_signal_ui_toggle_run(&demo);
        assert(memcmp(ch1, demo.ch1, sizeof(ch1)) == 0);
        if (zoom) demo_signal_ui_toggle_zoom(&demo);
        memcpy(ch1, demo.ch1, sizeof(ch1));
        memcpy(ch2, demo.ch2, sizeof(ch2));
        memcpy(minimum, demo.minimum, sizeof(minimum));
        memcpy(maximum, demo.maximum, sizeof(maximum));
        memcpy(source, demo.source_samples, sizeof(source));
        start = demo.capture_start_us;
        sequence = demo.capture_sequence;
        demo_signal_set_trigger_preview_y(&demo, 190);
        demo_signal_press(&demo, DEMO_ENC_TRIGGER, 0);
        assert(demo.screen.trigger_y == 190);
        demo_signal_press(&demo, DEMO_ENC_TRIGGER, 1);
        assert(demo.screen.trigger_y == SCOPE_PLOT_HEIGHT / 2);
        assert(demo_signal_set_option(&demo, DEMO_MENU_TRIGGER, 1, 1));
        assert(demo_signal_set_option(&demo, DEMO_MENU_TRIGGER, 2, 2));
        assert(memcmp(ch1, demo.ch1, sizeof(ch1)) == 0);
        assert(memcmp(ch2, demo.ch2, sizeof(ch2)) == 0);
        assert(memcmp(minimum, demo.minimum, sizeof(minimum)) == 0);
        assert(memcmp(maximum, demo.maximum, sizeof(maximum)) == 0);
        assert(memcmp(source, demo.source_samples, sizeof(source)) == 0);
        assert(start == demo.capture_start_us && sequence == demo.capture_sequence);
        demo_signal_force_trigger(&demo);
        assert(demo.capture_sequence == sequence + 1);
    }
}

static void test_stopped_time_view(void)
{
    static DemoSignal demo;
    static double source[2][DEMO_CAPTURE_POINTS];
    int16_t original[SCOPE_PLOT_WIDTH], compressed[SCOPE_PLOT_WIDTH];
    DemoWaveCapture wave, restored;
    uint32_t sequence;
    double start, step;
    int index, x, valid;
    demo_signal_init(&demo);
    demo_signal_advance(&demo);
    demo_signal_ui_toggle_run(&demo);
    index=demo.time_index;sequence=demo.capture_sequence;
    start=demo.capture_start_us;step=demo.source_step_us;
    memcpy(source,demo.source_samples,sizeof(source));
    memcpy(original,demo.ch1,sizeof(original));
    demo_signal_rotate(&demo,DEMO_ENC_TIME,-1); /* Twice the time span. */
    assert(demo.time_index==index+1 && !demo.screen.running);
    assert(demo.ch1[0]==WAVEFORM_NO_SAMPLE && demo.ch1[SCOPE_PLOT_WIDTH-1]==WAVEFORM_NO_SAMPLE);
    valid=0;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x)if(demo.ch1[x]!=WAVEFORM_NO_SAMPLE)++valid;
    assert(valid>=511 && valid<=514 && !demo.screen.history_count);
    memcpy(compressed,demo.ch1,sizeof(compressed));
    assert(strtod(demo.measurement_value_text[0],NULL)<10.0);
    assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,0,3));
    assert(demo.ch1[0]==WAVEFORM_NO_SAMPLE && demo.ch1[512]!=WAVEFORM_NO_SAMPLE);
    assert(strtod(demo.measurement_value_text[0],NULL)<10.0);
    assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,0,0));
    assert(!memcmp(compressed,demo.ch1,sizeof(compressed)));
    demo_signal_ui_toggle_zoom(&demo); /* Detail still uses the physical time scale. */
    assert(!memcmp(original,demo.ch1,sizeof(original)));
    demo_signal_ui_toggle_zoom(&demo);
    assert(!memcmp(compressed,demo.ch1,sizeof(compressed)));
    demo_signal_zoom_time(&demo,1);
    assert(!memcmp(original,demo.ch1,sizeof(original)));
    assert(demo_signal_set_option(&demo,DEMO_MENU_TIME,0,index+2)); /* Menu path, fivefold span. */
    valid=0;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x)if(demo.ch1[x]!=WAVEFORM_NO_SAMPLE)++valid;
    assert(valid>=204 && valid<=207);
    demo_signal_rotate(&demo,DEMO_ENC_TRIGGER,1);
    assert(demo.ch1[0]==WAVEFORM_NO_SAMPLE && demo.ch1[512]!=WAVEFORM_NO_SAMPLE);
    assert(!memcmp(source,demo.source_samples,sizeof(source)) && demo.capture_sequence==sequence);
    assert(demo.capture_start_us==start && demo.source_step_us==step);
    demo_signal_export_wave(&demo,&wave);
    FILE *file=tmpfile();assert(file && wave_file_write(file,&wave));rewind(file);
    memset(&restored,0,sizeof(restored));assert(wave_file_read(file,&restored));fclose(file);
    demo_signal_ui_show_wave(&demo,"stopped.csv",&restored);
    assert(!memcmp(wave.ch1,demo.ch1,sizeof(wave.ch1))); /* Same CSV schema, gaps survive replay. */
    demo_signal_rotate(&demo,DEMO_ENC_TIME,-1);
    assert(demo.time_index>restored.time_index && demo.ch1[0]==WAVEFORM_NO_SAMPLE);
    demo_signal_zoom_time(&demo,1);
    assert(!memcmp(wave.ch1,demo.ch1,sizeof(wave.ch1)));
}

static void test_time_range_and_csv_scale(void)
{
    static DemoSignal demo;
    DemoWaveCapture wave, restored;
    FILE *file;
    demo_signal_init(&demo);
    demo_signal_export_wave(&demo, &wave);
    /* A previous CSV used index 3 for 100 us/div. Its physical scale wins. */
    wave.time_index = 3;
    file = tmpfile();
    assert(file && wave_file_write(file, &wave));
    rewind(file);
    memset(&restored, 0, sizeof(restored));
    assert(wave_file_read(file, &restored));
    fclose(file);
    demo_signal_ui_show_wave(&demo, "legacy.csv", &restored);
    assert(strcmp(demo.screen.time_scale, "100 us") == 0);
    demo_signal_export_wave(&demo, &restored);
    assert(restored.time_us_per_div == 100.0);
    assert(memcmp(restored.ch1, wave.ch1, sizeof(wave.ch1)) == 0);

    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo, 100);
    assert(strcmp(demo.screen.time_scale, "10 ns") == 0);
    demo_signal_export_wave(&demo, &wave);
    assert(wave.time_us_per_div == 0.01);
    demo_signal_zoom_time(&demo, 1);
    assert(strcmp(demo.screen.time_scale, "10 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "20 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "50 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "100 ns") == 0);
    demo_signal_zoom_time(&demo, -100);
    assert(strcmp(demo.screen.time_scale, "5 s") == 0 && demo.screen.rolling);
    demo_signal_export_wave(&demo, &wave);
    assert(wave.time_us_per_div == 5000000.0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "5 s") == 0 && demo.screen.rolling);
}

static void test_zoom_source_detail(void)
{
    static DemoSignal demo;
    int16_t overview[SCOPE_PLOT_WIDTH], detail[SCOPE_PLOT_WIDTH];
    DemoWaveCapture wave;
    uint32_t sequence;
    int x, narrow = 0, distinct = 0;
    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo, -6); /* 10 ms/div: each column covers almost a period. */
    demo_signal_ui_toggle_run(&demo);
    sequence = demo.capture_sequence;
    memcpy(overview, demo.ch1, sizeof(overview));
    demo_signal_ui_toggle_zoom(&demo);
    demo_signal_zoom_time(&demo, 8); /* 10 us/div, from the same captured source. */
    assert(strcmp(demo.screen.time_scale, "10 us") == 0);
    assert(memcmp(overview, demo.saved_ch1, sizeof(overview)) == 0);
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x) {
        if (demo.maximum[0][x] - demo.minimum[0][x] < 10) ++narrow;
        if (demo.ch1[x] != demo.ch1[x - 1]) ++distinct;
    }
    assert(narrow > 900 && distinct > 20);
    memcpy(detail, demo.ch1, sizeof(detail));
    demo_signal_zoom_time(&demo, 1);
    assert(strcmp(demo.screen.time_scale, "5 us") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(memcmp(detail, demo.ch1, sizeof(detail)) == 0);
    demo_signal_ui_pan_time(&demo, 1);
    assert(fabs(demo.zoom_offset + 0.001) < 1e-9);
    assert(demo.screen.trigger_marker_x == 513);
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x)
        assert(abs(demo.ch1[x] - detail[x - 1]) <= 1);
    assert(demo.capture_sequence == sequence && !demo.screen.running);
    demo_signal_export_wave(&demo, &wave);
    assert(wave.time_us_per_div == 10.0 && wave.time_index == demo.zoom_time_index);
    demo_signal_zoom_time(&demo, 100);
    assert(strcmp(demo.screen.time_scale, "10 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "20 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "50 ns") == 0);
    demo_signal_zoom_time(&demo, -1);
    assert(strcmp(demo.screen.time_scale, "100 ns") == 0);
    demo_signal_set_zoom_center(&demo, 0);
    assert(demo.screen.zoom_window_start == 0);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_TIME);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_menu_adjust(&demo, demo.time_index - demo.zoom_time_index);
    assert(strcmp(demo.screen.menu_values[0], "10 ms") == 0);
    demo_signal_ui_menu_activate(&demo);
    demo_signal_ui_dismiss_menu(&demo);
    assert(strcmp(demo.screen.time_scale, "10 ms") == 0);
    assert(demo.zoom_offset == 0.0);
    demo_signal_ui_toggle_zoom(&demo);
    assert(memcmp(overview, demo.ch1, sizeof(overview)) == 0);
}

static void test_zoom_noise_source_grid(void)
{
    static DemoSignal demo;
    int x, changes = 0, longest = 0, run = 0;
    int16_t stopped[SCOPE_PLOT_WIDTH];
    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo, 6); /* Fast base acquisition, not a zoom into a slower one. */
    demo_signal_ui_toggle_run(&demo);
    demo_signal_ui_toggle_zoom(&demo);
    assert(strcmp(demo.screen.time_scale, "500 ns") == 0);
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x) {
        if (demo.ch2[x] != demo.ch2[x - 1]) { ++changes; run = 0; }
        else if (++run > longest) longest = run;
    }
    /* Source noise must not repeat for the width of a former overview pixel. */
    assert(changes > 400 && longest < 15);
    memcpy(stopped, demo.ch2, sizeof(stopped));
    demo_signal_zoom_time(&demo, -1);
    demo_signal_zoom_time(&demo, 1);
    assert(memcmp(stopped, demo.ch2, sizeof(stopped)) == 0);
}

static void test_adaptive_capture(void)
{
    static DemoSignal demo;
    static double stopped[2][DEMO_CAPTURE_POINTS];
    int16_t low[SCOPE_PLOT_WIDTH], high[SCOPE_PLOT_WIDTH];
    size_t x, count;
    double step;
    int changed = 0;
    demo_signal_init(&demo);
    assert(demo.source_count == DEMO_CAPTURE_POINTS);
    assert(fabs(demo.source_step_us - 1000.0 / DEMO_CAPTURE_POINTS) < 1e-12);
    memcpy(low, demo.minimum[0], sizeof(low));
    memcpy(high, demo.maximum[0], sizeof(high));
    demo_signal_advance(&demo);
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        if (low[x] != demo.minimum[0][x] || high[x] != demo.maximum[0][x]) ++changed;
    assert(changed > 200); /* RUN changes real envelope values, not just a hidden center trace. */
    demo_signal_ui_toggle_run(&demo);
    count = demo.source_count;
    step = demo.source_step_us;
    memcpy(stopped, demo.source_samples, sizeof(stopped));
    demo_signal_ui_toggle_zoom(&demo);
    demo_signal_zoom_time(&demo, 100);
    assert(demo.source_count == count && demo.source_step_us == step);
    assert(!memcmp(stopped, demo.source_samples, sizeof(stopped)));
    demo_signal_ui_toggle_zoom(&demo);
    demo_signal_zoom_time(&demo, 100);
    assert(demo.source_count == count && demo.source_step_us == step);
    assert(!memcmp(stopped, demo.source_samples, sizeof(stopped)));
    demo_signal_force_trigger(&demo);
    assert(demo.source_count == 20 && fabs(demo.source_step_us - 0.005) < 1e-12);
    demo_signal_zoom_time(&demo, -100);
    demo_signal_force_trigger(&demo);
    assert(demo.source_count == DEMO_CAPTURE_POINTS);
    assert(fabs(demo.source_step_us - 5e7 / DEMO_CAPTURE_POINTS) < 1e-9);
}

static void test_measurements_beside_cursors(void)
{
    static DemoSignal demo;
    int x, y, width, height, cursor_width, cursor_y;
    demo_signal_init(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_toggle_measurement_layout(&demo);
    demo_signal_ui_move_measurements(&demo, 3000, 3000);
    scope_screen_measurement_bounds(&demo.screen, &x, &y, &width, &height);
    scope_screen_cursor_measurement_bounds(&demo.screen, NULL, &cursor_y, &cursor_width, NULL);
    assert(x >= cursor_width && y + height == SCOPE_MEASURE_BOTTOM_Y);
    demo_signal_ui_toggle_measurement_layout(&demo);
    scope_screen_measurement_bounds(&demo.screen, &x, &y, &width, &height);
    assert(x < cursor_width && y + height == cursor_y);

    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.cursor_measurement_rows == 2);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "1.500 V") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[1][2], "3.000 V") == 0);
    demo_signal_zoom_channel(&demo, 0, 1);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "3.000 V") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[1][2], "3.000 V") == 0);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_CURSOR);
    assert(demo.screen.menu_count == 1);
}

static void test_cursor_precision(void)
{
    static DemoSignal demo;
    char before[24];
    demo_signal_init(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_zoom_time(&demo, 7);
    demo_signal_ui_select_cursor(&demo, 0);
    demo_signal_ui_move_cursor(&demo, 300);
    demo_signal_ui_select_cursor(&demo, 1);
    demo_signal_ui_move_cursor(&demo, 710);
    assert(strcmp(demo.screen.cursor_measurement_values[0][1], "+966.8 ns") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "2.002 us") == 0);
    strcpy(before, demo.screen.cursor_measurement_values[0][2]);
    demo_signal_ui_move_cursor(&demo, 711);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "2.007 us") == 0);
    assert(strcmp(before, demo.screen.cursor_measurement_values[0][2]) != 0);
    demo_signal_zoom_time(&demo, -9); /* 500 us/div */
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "2.007 ms") == 0);
    demo_signal_ui_move_cursor(&demo, 710);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "2.002 ms") == 0);
    demo_signal_ui_move_cursor(&demo, 300);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "0.000 us") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[0][3], "--") == 0);
}

static void test_compressed_generator(void)
{
    static DemoSignal demo;
    int shape, x;
    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo, -100);
    for (shape = DEMO_WAVE_SQUARE; shape <= DEMO_WAVE_SINC; ++shape) {
        demo_signal_ui_open_menu(&demo, DEMO_MENU_GENERATOR);
        while (demo.generator_wave[0] != shape)
            demo_signal_ui_menu_tap(&demo, 0, 1);
        for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
            size_t i;
            double low = demo.source_samples[0][8 * x];
            double high = low;
            for (i = 8 * x + 1; i < 8 * (x + 1); ++i) {
                if (demo.source_samples[0][i] < low) low = demo.source_samples[0][i];
                if (demo.source_samples[0][i] > high) high = demo.source_samples[0][i];
            }
            assert(demo.minimum[0][x] == (int16_t)(200.0 - high * 120.0));
            assert(demo.maximum[0][x] == (int16_t)(200.0 - low * 120.0));
            assert(demo.minimum[0][x] <= demo.ch1[x]);
            assert(demo.maximum[0][x] >= demo.ch1[x]);
        }
        demo_signal_ui_dismiss_menu(&demo);
    }
}

static void test_generator_megahertz_and_fft(void)
{
    static DemoSignal demo;
    static const char *frequencies[] = {
        "1 Hz", "2 Hz", "5 Hz", "10 Hz", "20 Hz", "50 Hz", "100 Hz", "200 Hz", "500 Hz",
        "1 kHz", "2 kHz", "5 kHz", "10 kHz", "20 kHz", "50 kHz",
        "100 kHz", "200 kHz", "500 kHz", "1 MHz"
    };
    int i, peak;
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_GENERATOR);
    demo_signal_ui_menu_tap(&demo, 0, 1); /* Sine, without square harmonics. */
    for (i = 13; i < 19; ++i) {
        demo_signal_ui_menu_tap(&demo, 2, 1);
        assert(demo.generator_frequency_index == i);
        assert(strcmp(demo.screen.menu_values[2], frequencies[i]) == 0);
    }
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    assert(strstr(demo.screen.fft_sampling_info, "ALIAS") != NULL);
    demo_signal_zoom_time(&demo, 2); /* 20 us/div: Fs 5.12 MHz. */
    assert(strcmp(demo.screen.fft_frequency_labels[8], "2.56M") == 0);
    assert(strcmp(demo.screen.fft_sampling_info, "FS 5.12M / DF 5k") == 0);
    peak = 0;
    for (i = 1; i < SCOPE_FFT_BINS; ++i)
        if (demo.fft_ch1[i] > demo.fft_ch1[peak]) peak = i;
    assert(fabs(spectrum_frequency_at((double)peak/(SCOPE_FFT_BINS-1),2560000,1024)-1000000)<15000); /* 1 MHz in the 0..2.56 MHz spectrum. */
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_CURSOR);
    assert(strcmp(demo.screen.menu_values[0], "VERTICAL") == 0);
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_GENERATOR);
    demo_signal_ui_menu_tap(&demo, 2, 1); /* Wrap at 1 MHz. */
    assert(demo.generator_frequency_index == 0);
    for (i = 0; i < 19; ++i) {
        assert(strcmp(demo.screen.menu_values[2], frequencies[i]) == 0);
        demo_signal_ui_menu_tap(&demo, 2, 1);
    }
    assert(demo.generator_frequency_index == 0);
    demo_signal_ui_menu_tap(&demo, 2, -1);
    assert(demo.generator_frequency_index == 18);
}

static void test_fft_cursor(void)
{
    static DemoSignal demo;
    demo_signal_init(&demo);
    demo_signal_ui_move_fft_cursor(&demo, 512);
    assert(!demo.screen.fft_cursor_visible);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    assert(!demo.screen.fft_cursor_visible);
    demo_signal_ui_move_fft_cursor(&demo, 600);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(!demo.screen.fft_cursor_visible);
    assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
    assert(demo.screen.fft_cursor_x == 512);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.fft_cursor_visible); /* Appears without touching FFT. */
    demo_signal_ui_move_fft_cursor(&demo, 512);
    assert(demo.screen.fft_cursor_visible && demo.screen.fft_cursor_x == 512);
    assert(strcmp(demo.screen.fft_cursor_value, "21.72 kHz") == 0);
    demo_signal_ui_move_fft_cursor(&demo, 513);
    assert(strcmp(demo.screen.fft_cursor_value, "21.86 kHz") == 0);
    demo_signal_set_fft_range(&demo, 100000000.0, 65536); /* Raw adapter changes Fs and FFT length. */
    assert(strcmp(demo.screen.fft_cursor_value, "557.9 kHz") == 0);
    demo_signal_ui_move_fft_cursor(&demo, -100);
    assert(demo.screen.fft_cursor_x == 0);
    assert(strcmp(demo.screen.fft_cursor_value, "0 Hz") == 0);
    demo_signal_ui_move_fft_cursor(&demo, 3000);
    assert(demo.screen.fft_cursor_x == 1023);
    assert(strcmp(demo.screen.fft_cursor_value, "100.0 MHz") == 0);
    demo_signal_zoom_time(&demo, 2);
    assert(strcmp(demo.screen.fft_cursor_value, "2.560 MHz") == 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_TIME);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(demo.screen.fft_cursor_visible);
    demo_signal_ui_cycle_cursor_mode(&demo);
    assert(!demo.screen.fft_cursor_visible);
    assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
    demo_signal_ui_move_fft_cursor(&demo, 600);
    assert(!demo.screen.fft_cursor_visible && demo.screen.fft_cursor_x == 1023);
}

static void test_cursor_encoder_direction(void)
{
    static DemoSignal demo;
    demo_signal_init(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.cursor_a == 248); /* TIME: positive rotation moves right. */
    demo_signal_ui_cycle_cursor_mode(&demo);
    for (int selected = 0; selected < 2; ++selected) {
        demo_signal_ui_select_cursor(&demo, selected);
        for (int fine = 0; fine < 2; ++fine) {
            demo.screen.fine_mode = fine;
            demo_signal_ui_move_cursor(&demo, 200);
            demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
            assert((selected ? demo.screen.cursor_b : demo.screen.cursor_a) == (fine ? 199 : 192));
            demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
            assert((selected ? demo.screen.cursor_b : demo.screen.cursor_a) == 200);
            demo_signal_ui_move_cursor(&demo, 0);
            demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
            assert((selected ? demo.screen.cursor_b : demo.screen.cursor_a) == 0);
            demo_signal_ui_move_cursor(&demo, SCOPE_PLOT_HEIGHT - 1);
            demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
            assert((selected ? demo.screen.cursor_b : demo.screen.cursor_a) == SCOPE_PLOT_HEIGHT - 1);
        }
    }
}

static void test_fft_encoder_cycle(void)
{
    static DemoSignal demo;
    int mode, a, b;
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    for (mode = SCOPE_CURSOR_TIME; mode <= SCOPE_CURSOR_VOLTAGE; ++mode) {
        demo_signal_ui_cycle_cursor_mode(&demo);
        assert(demo.screen.cursor_mode == mode);
        assert(demo.screen.fft_cursor_visible);
        assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
        demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
        assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_B);
        demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
        assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_FFT);
        assert(demo.screen.fft_cursor_visible);
        a = demo.screen.cursor_a;
        b = demo.screen.cursor_b;
        demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
        assert(demo.screen.cursor_a == a && demo.screen.cursor_b == b);
        if (mode == SCOPE_CURSOR_TIME) assert(demo.screen.fft_cursor_x == 520);
        else {
            assert(demo.screen.fft_cursor_level == 136);
            assert(strcmp(demo.screen.fft_cursor_value, "-37.33 dB") == 0);
            demo_signal_ui_move_fft_cursor(&demo, -30);
            assert(strcmp(demo.screen.fft_cursor_value, "-80.00 dB") == 0);
            demo_signal_ui_move_fft_cursor(&demo, 300);
            assert(strcmp(demo.screen.fft_cursor_value, "0.000 dB") == 0);
            demo_signal_ui_toggle_fine(&demo);
            demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
            assert(demo.screen.fft_cursor_level == 254);
        }
        demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
        assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
    }
    demo_signal_ui_move_fft_cursor(&demo, 128);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 0);
    demo_signal_ui_dismiss_menu(&demo);
    assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
    assert(!demo.screen.fft_cursor_visible);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_B);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.cursor_selected == SCOPE_CURSOR_SELECT_A);
    /* Enabling FFT after cursors also shows its cursor immediately. */
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    assert(demo.screen.fft_cursor_visible);
}

static void test_adc_level_menu(void)
{
    static DemoSignal demo;
    const uint8_t code[2]={0,255};
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_DEBUG);
    assert(!strcmp(demo.screen.menu_values[4],"-") && !strcmp(demo.screen.menu_values[5],"-"));
    assert(demo_signal_set_adc_level(&demo,code,1));
    assert(!strcmp(demo.screen.menu_labels[4],"ADC CH1") && !strcmp(demo.screen.menu_values[4],"0"));
    assert(!strcmp(demo.screen.menu_labels[5],"ADC CH2") && !strcmp(demo.screen.menu_values[5],"255"));
    assert(!demo_signal_set_adc_level(&demo,code,1));
    for(int row=4;row<6;++row){
        assert(!demo.screen.menu_stepper[row]);
        assert(demo.screen.menu_disabled[row]);
        demo_signal_ui_menu_tap(&demo,row,1);
        assert(demo.menu_kind==DEMO_MENU_DEBUG);
        demo_signal_ui_menu_select(&demo,row);
        demo_signal_ui_menu_activate(&demo);
        demo_signal_ui_menu_adjust(&demo,1);
        assert(!demo.screen.menu_editing);
        assert(!demo_signal_set_option(&demo,DEMO_MENU_DEBUG,row,0));
    }
    assert(demo.adc_simulation==1 && demo.hold_time_ms==300 && demo.double_click_ms==300);
    assert(demo_signal_set_adc_level(&demo,code,0));
    assert(!strcmp(demo.screen.menu_values[4],"-") && !strcmp(demo.screen.menu_values[5],"-"));
}

int main(void)
{
    DemoSignal demo;
    static uint32_t pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
    static uint32_t capture_pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
    int16_t synchronized_ch1[SCOPE_PLOT_WIDTH];
    int previous, x, y;
    int edge;
    int i;

    assert(battery_level_percent(0)==0 && battery_level_percent(3300)==0);
    assert(battery_level_percent(3850)==60 && battery_level_percent(4200)==100);
    assert(battery_level_percent(6600)==100);
    for(i=3301;i<4200;++i)assert(battery_level_percent(i)>=battery_level_percent(i-1));

    test_time_range_and_csv_scale();
    test_zoom_source_detail();
    test_zoom_noise_source_grid();
    test_adaptive_capture();
    test_measurements_beside_cursors();
    test_cursor_precision();
    test_compressed_generator();
    test_generator_megahertz_and_fft();
    test_fft_cursor();
    test_fft_encoder_cycle();
    test_cursor_encoder_direction();
    test_scale_direction_and_stopped_trigger();
    test_adc_level_menu();
    test_stopped_time_view();
    test_menu_button_holds();
    test_default_double_click();

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
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1300);
    assert(!demo.screen.ch1_enabled && !demo.screen.menu_open);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 2500);
    assert(demo.screen.ch1_enabled && !demo.screen.menu_open);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 3500);
    assert(demo.menu_kind == DEMO_MENU_CH1);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 4500);
    assert(!demo.screen.menu_open && demo.screen.ch1_enabled);

    demo_signal_rotate(&demo, DEMO_ENC_CH1, -1);
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
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.trigger_edge_index == 1);
    assert(strcmp(demo.screen.trigger_source, "CH2 FALL") == 0);
    assert(demo.screen.trigger_edge_falling);
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_press(&demo, DEMO_ENC_TRIGGER, 1);
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
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 1);
    assert(demo.time_index == previous - 1);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, -1);
    assert(demo.time_index == previous);
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
    assert(demo.zoom_time_index == previous - 3 && demo.time_index == previous);
    assert(strcmp(demo.screen.time_scale, "10 us") == 0);
    assert(demo.screen.zoom_window_start == 460);
    assert(demo.screen.zoom_window_end == 564);
    demo_signal_press(&demo, DEMO_ENC_TIME, 0);
    demo_signal_rotate(&demo, DEMO_ENC_TIME, 1);
    assert(fabs(demo.zoom_offset - 1.6) < 1e-9 && demo.screen.trigger_y == 128);
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
    assert(strcmp(demo.screen.cursor_measurement_values[0][2], "460.9 us") == 0);
    assert(strcmp(demo.screen.cursor_measurement_values[0][3], "2.169 kHz") == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.cursor_selected);
    demo_signal_ui_move_cursor(&demo, demo.screen.cursor_a);
    assert(strcmp(demo.screen.cursor_measurement_values[0][3], "--") == 0);
    demo_signal_press(&demo, DEMO_BTN_CURSOR, 1);
    assert(demo.menu_kind == DEMO_MENU_CURSOR && demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE);
    assert(demo.screen.menu_count == 1);
    assert(demo.screen.cursor_measurement_rows == 2);
    assert(strcmp(demo.screen.cursor_measurement_values[0][2],
                  demo.screen.cursor_measurement_values[1][2]) != 0);
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
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(strcmp(demo.screen.ch2_input, "AC   x1") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(strcmp(demo.screen.ch2_input, "AC   x10") == 0);
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_rotate(&demo, DEMO_ENC_CH2, 1);
    demo_signal_press(&demo, DEMO_ENC_CH2, 0);
    demo_signal_rotate(&demo, DEMO_ENC_CH2, 1);
    assert(demo.channel_position[1] == -8);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_CH2);demo_signal_ui_menu_select(&demo,2);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -2);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind==DEMO_MENU_CALIBRATION && demo.screen.menu_count==3 && demo.response_channel==1 && demo.channel_position[1]==-8);
    demo_signal_ui_menu_back(&demo);
    demo_signal_ui_reset_channel_position(&demo,1);
    demo_signal_ui_menu_select(&demo,1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.coupling[1] == 0 && demo.screen.menu_option_count[1] == 2);
    assert(demo.screen.channel_zero_y[1] == 240);
    assert(demo.ch2[0] != demo.ch2[50]);
    demo_signal_press(&demo, DEMO_ENC_CH2, 1);
    assert(demo.screen.channel_zero_y[1] == 240);

    demo_signal_init(&demo);
    demo_signal_press_at(&demo, DEMO_BTN_CH1, 0, 1000);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -3);
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
    assert(demo.time_index == 11);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    assert(!demo.screen.menu_open && !demo.screen.zoom_enabled);
    demo_signal_press(&demo, DEMO_BTN_MENU, 1);
    demo_signal_zoom_time(&demo, 2);
    assert(strcmp(demo.screen.time_scale, "5 us") == 0);
    demo_signal_set_zoom_center(&demo, 600);
    assert(demo.screen.zoom_window_start == 548);
    assert(demo.screen.zoom_window_end == 652);

    demo_signal_init(&demo);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(demo.screen.menu_count == 7);
    assert(strcmp(demo.screen.menu_labels[0], "SETUP") == 0);
    assert(strcmp(demo.screen.menu_labels[1], "BROWSE") == 0);
    assert(strcmp(demo.screen.menu_labels[3], "MEASUREMENTS") == 0);
    assert(strcmp(demo.screen.menu_labels[4], "PROCESSING") == 0);
    assert(strcmp(demo.screen.menu_labels[6], "DEBUG") == 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_DEBUG && demo.menu_parent == DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_BTN_MENU, 0);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
    assert(demo.menu_kind == DEMO_MENU_MEASURE);
    assert(demo.screen.measurement_menu && demo.screen.menu_count == 18);
    assert(strcmp(demo.screen.menu_labels[0], "VPP") == 0);
    assert(strcmp(demo.screen.menu_labels[8], "VPP") == 0);
    assert(strcmp(demo.screen.menu_labels[16], "REMOVE ALL") == 0);
    assert(demo.screen.menu_page == NULL);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -8);
    assert(demo.measurement_selected == DEMO_MEAS_CH2_VPP);
    assert(demo.screen.menu_selected == 8);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 2);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 8);
    assert(demo.measurement_selected == DEMO_MEAS_CH1_VPP);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.measurement_count == 1);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.screen.menu_selected==SCOPE_MEASURE_HIDE_INDEX);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.screen.measurement_hidden && !strcmp(demo.screen.menu_labels[SCOPE_MEASURE_HIDE_INDEX],"SHOW"));
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(!demo.screen.measurement_hidden);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
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
    assert(demo.time_index == 11 && !demo.screen.menu_editing);
    demo_signal_ui_pan_time(&demo, 35);
    assert(demo.time_position == -35);
    demo_signal_ui_menu_select(&demo, 1);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.time_position == 0);
    assert(demo.screen.menu_count == 3 && !strcmp(demo.screen.menu_labels[2], "SAMPLE RATE"));
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    assert(demo.screen.menu_count == 6 && !strcmp(demo.screen.menu_labels[5], "RECORD LENGTH"));
    demo_signal_ui_menu_select(&demo, 5);
    previous = demo.record_depth_index;
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.record_depth_index == (previous + 1) % CAPTURE_DEPTH_COUNT);
    assert(!demo.screen.menu_editing);
    demo_signal_ui_menu_back(&demo);
    assert(!demo.screen.menu_open);

    demo_signal_ui_open_menu(&demo, DEMO_MENU_CURSOR);
    assert(demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_TIME && demo.screen.menu_count == 1);
    demo_signal_ui_menu_tap(&demo, 0, 0);
    assert(demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE && demo.screen.menu_count == 1);
    assert(demo.screen.cursor_measurement_rows == 2);
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
    assert(demo.time_index == 13 && !demo.screen.menu_editing);
    demo_signal_ui_menu_tap(&demo, 0, -1);
    assert(demo.time_index == 12);

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
    assert(demo.screen.menu_count == 6);
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
        static uint8_t density[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
        static uint32_t plain[SCOPE_WIDTH*SCOPE_HEIGHT];
        int x;
        uint32_t common, rare;
        for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
            density[x][200] = (uint8_t)waveform_density_level(7,7);
            density[x][220] = (uint8_t)waveform_density_level(1,7);
        }
        screen.ch1_enabled = 1;
        screen.intensity_coloring = 1;
        screen.density_levels[0] = &density[0][0];
        screen.density_rows = SCOPE_PLOT_HEIGHT;
        scope_screen_render(pixels, SCOPE_WIDTH, &screen);
        common = pixels[(SCOPE_PLOT_Y + 200) * SCOPE_WIDTH + 300];
        rare = pixels[(SCOPE_PLOT_Y + 220) * SCOPE_WIDTH + 300];
        assert(((common >> 16) & 255) > (common & 255));
        assert((rare & 255) > ((rare >> 16) & 255));
        assert(pixels[(SCOPE_PLOT_Y+210)*SCOPE_WIDTH+301] != common);
        screen.zoom_enabled=1;screen.split_height=200;
        scope_screen_render(pixels,SCOPE_WIDTH,&screen);
        screen.running=0;
        scope_screen_render(plain,SCOPE_WIDTH,&screen);
        assert(memcmp(plain,pixels,sizeof(plain))==0);
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
    for (i = 0; i < 200; ++i) demo_signal_advance(&demo);
    assert(demo.screen.status_visible);
    demo_signal_update_time(&demo, 2999);
    assert(demo.screen.status_visible);
    demo_signal_update_time(&demo, 3000);
    assert(!demo.screen.status_visible);
    demo_signal_notify(&demo, "MEASUREMENT LIST FULL / REMOVE ONE");
    assert(demo.screen.status_warning && demo.screen.status_alpha == 255);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    previous = (int)pixels[65 * SCOPE_WIDTH + 300];
    assert((previous >> 16 & 255) > (previous >> 8 & 255));
    demo_signal_update_time(&demo, 5750);
    assert(demo.screen.status_visible && demo.screen.status_alpha < 255);
    scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
    assert((int)pixels[65 * SCOPE_WIDTH + 300] != previous);
    demo_signal_update_time(&demo, 6000);
    assert(!demo.screen.status_visible);
    assert(demo.screen.measurement_horizontal);
    assert(demo.screen.measurement_x == 0 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 62);
    demo_signal_ui_move_measurements(&demo, 100, -80);
    assert(demo.screen.measurement_x == 100 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_ui_toggle_measurement_layout(&demo);
    assert(!demo.screen.measurement_horizontal);
    assert(demo.screen.measurement_x == 100);
    scope_screen_measurement_bounds(&demo.screen, NULL, NULL, &previous, NULL);
    assert(previous < 984);
    demo_signal_ui_toggle_measurements_visible(&demo);
    assert(demo.screen.measurement_hidden && demo.measurement_count == 3);
    scope_screen_measurement_bounds(&demo.screen, &i, &edge, &x, &y);
    assert(i == 0 && edge == 0 && x == 0 && y == 0);
    assert(demo.screen.measurement_x == 100 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_ui_move_measurements(&demo, 300, 100);
    assert(demo.screen.measurement_x == 100 &&
           demo.screen.measurement_y == SCOPE_MEASURE_BOTTOM_Y - 142);
    demo_signal_advance(&demo);
    assert(demo.screen.measurement_hidden && demo.measurement_count == 3);
    demo_signal_ui_toggle_measurements_visible(&demo);
    assert(!demo.screen.measurement_hidden && demo.measurement_count == 3);
    assert(demo.screen.measurement_x == 100 &&
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
    demo_signal_ui_move_measurements(&demo, -3000, 3000);
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
    assert(i + edge == SCOPE_MEASURE_BOTTOM_Y - 66);
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
        static const char *values[]={"OFF","--","0.00 V","-999.99 mV","100.00 MHz","123456789 V"};
        int saved_font=demo.screen.font_index,saved_layout=demo.screen.measurement_horizontal;
        const char *saved_value = demo.screen.measurement_values[7];
        for(int font=0;font<2;++font)for(int layout=0;layout<2;++layout){
            int width,height;
            demo.screen.font_index=font;demo.screen.measurement_horizontal=layout;
            scope_screen_measurement_bounds(&demo.screen,NULL,NULL,&width,&height);
            for(unsigned value=0;value<sizeof(values)/sizeof(*values);++value){
                demo.screen.measurement_values[7]=values[value];
                scope_screen_measurement_bounds(&demo.screen,NULL,NULL,&previous,&edge);
                assert(previous==width && edge==height);
            }
        }
        demo.screen.measurement_values[7] = saved_value;
        demo.screen.font_index=saved_font;demo.screen.measurement_horizontal=saved_layout;
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
        assert(prefixed_width == previous && previous < 600);
    }

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_select(&demo, 4);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.fft_enabled && !demo.screen.menu_editing);
    {
        unsigned peak=0;
        for(unsigned bin=1;bin<SCOPE_FFT_BINS;++bin)
            if(demo.screen.fft_ch1_bins[bin]>demo.screen.fft_ch1_bins[peak])peak=bin;
        assert(fabs(spectrum_frequency_at((double)peak/(SCOPE_FFT_BINS-1),512000,1024)-10000)<2000);
    }
    demo_signal_ui_set_split_height(&demo, 170);
    assert(demo.screen.split_height == 170);
    demo_signal_ui_toggle_zoom(&demo);
    assert(demo.screen.zoom_enabled && !demo.screen.fft_enabled);
    demo_signal_ui_set_split_height(&demo, 180);
    assert(demo.screen.split_height == 180);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DISPLAY);
    assert(demo.screen.menu_count == 3);
    assert(!strcmp(demo.screen.menu_labels[0], "LAYOUT"));
    assert(!strcmp(demo.screen.menu_labels[1], "GRID"));
    assert(!strcmp(demo.screen.menu_labels[2], "GRID BRIGHTNESS"));
    assert(demo.screen.grid_brightness == 5);
    demo_signal_ui_menu_tap(&demo, 2, 1);
    assert(demo.screen.grid_brightness == 6 && !strcmp(demo.screen.menu_values[2], "6"));
    assert(demo_signal_set_option(&demo, DEMO_MENU_DISPLAY, 2, 10));
    demo_signal_ui_menu_tap(&demo, 2, 1);
    assert(demo.screen.grid_brightness == 10);
    assert(!demo_signal_set_option(&demo, DEMO_MENU_DISPLAY, 2, 11));
    assert(demo_signal_set_option(&demo, DEMO_MENU_DISPLAY, 2, 0));
    demo_signal_ui_menu_tap(&demo, 2, -1);
    assert(demo.screen.grid_brightness == 0);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_DEBUG);
    assert(demo.screen.menu_count == 6);
    assert(demo.screen.font_index == SCOPE_FONT_INTER);
    demo_signal_ui_menu_select(&demo, 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_GENERATOR);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    demo_signal_ui_dismiss_menu(&demo);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    assert(demo.screen.menu_count == 7);
    previous = (SCOPE_WIDTH - SCOPE_MENU_WIDTH) / 2;
    edge = (SCOPE_HEIGHT - (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
            demo.screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6)) / 2;
    assert(demo.screen.menu_x == previous && demo.screen.menu_y == edge);
    demo_signal_ui_move_menu(&demo, -1000, -1000);
    assert(demo.screen.menu_x == 0 && demo.screen.menu_y == 0);
    demo_signal_ui_move_menu(&demo, 1000, 1000);
    assert(demo.screen.menu_x == SCOPE_WIDTH - SCOPE_MENU_WIDTH);
    assert(demo.screen.menu_y == SCOPE_HEIGHT -
           (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
            demo.screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6));
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    assert(demo.screen.menu_x == previous && demo.screen.menu_y == edge);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -3);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_MEASURE && demo.menu_parent == DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    assert(demo.screen.menu_x == previous && demo.screen.menu_y == edge);

    demo_signal_ui_menu_select(&demo, 6);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    demo_signal_ui_menu_activate(&demo);
    assert(demo.menu_kind == DEMO_MENU_GENERATOR);
    assert(demo.screen.menu_count == 3);
    assert(demo.screen.menu_option_count[0] == DEMO_WAVE_COUNT &&
           demo.screen.menu_option_count[1] == DEMO_WAVE_COUNT);
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
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.menu_editing && demo.generator_frequency_index == 12);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.edit_value == 13);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.generator_frequency_index == 13);
    assert(!demo.screen.menu_editing);
    assert(strcmp(demo.screen.menu_values[2], "20 kHz") == 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -1);
    demo_signal_ui_menu_back(&demo);
    assert(!demo.screen.menu_editing && demo.generator_frequency_index == 13);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_DEBUG);
    demo_signal_ui_menu_back(&demo);
    assert(demo.menu_kind == DEMO_MENU_MAIN);
    demo_signal_rotate(&demo, DEMO_ENC_FUNCTION, -4);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_PROCESSING && demo.menu_parent == DEMO_MENU_MAIN);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 1);
    assert(demo.menu_kind == DEMO_MENU_MAIN);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    demo_signal_ui_menu_select(&demo,1);
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
                  SCOPE_BROWSE_CONFIRM_X + 20] == 0xf55d6d);
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
    demo_signal_ui_menu_select(&demo, 0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_SETUP);
    demo_signal_ui_menu_select(&demo, 2);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.menu_kind == DEMO_MENU_PC);
    demo_signal_pc_status(&demo,1,115200,0);
    demo_signal_press(&demo, DEMO_ENC_FUNCTION, 0);
    assert(demo.screen.menu_editing && demo.pc_baud_index==4);
    demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,1);
    assert(demo.pc_baud_index==4);
    demo_signal_press(&demo,DEMO_ENC_FUNCTION,0);
    assert(demo.pc_baud_index == 5 && !demo.screen.menu_editing);
    assert(strcmp(demo.screen.menu_labels[0], "UART BAUD") == 0);
    assert(strcmp(demo.screen.menu_values[0], "230400") == 0);
    demo_signal_pc_status(&demo, 1, 1000000, 0);
    assert(strcmp(demo.screen.menu_values[0], "1000000") == 0);
    assert(strcmp(demo.screen.menu_values[1], "USB-UART READY") == 0);
    demo_signal_pc_status(&demo, 1, 1000000, 1);
    assert(strcmp(demo.screen.menu_values[1], "PC ACTIVE") == 0);

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
    /* Rising and falling edges have vertically mirrored geometry. */
    for (i = 0; i < 10 * 12; ++i)
        assert((pixels[(545 + i / 10) * SCOPE_WIDTH + 778 + i % 10] == 0xffc44d) ==
               (pixels[(556 - i / 10) * SCOPE_WIDTH + 788 + i % 10] == 0xffc44d));
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
        assert(pixels[(SCOPE_BOTTOM_Y + 1) * SCOPE_WIDTH + 320] == 0xffc44d);
        assert(pixels[(SCOPE_BOTTOM_Y + 1) * SCOPE_WIDTH + 860] == 0xb696f1);
        assert(pixels[546 * SCOPE_WIDTH + 82] == 0x6977ad);
        demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
        assert(demo.screen.menu_count == 6);
        assert(strcmp(demo.screen.menu_labels[0], "SETUP") == 0);
        demo_signal_ui_menu_activate(&demo);
        assert(demo.menu_kind == DEMO_MENU_SETUP);
        demo_signal_ui_menu_back(&demo);
        assert(demo.menu_kind == DEMO_MENU_MAIN);
        demo_signal_ui_dismiss_menu(&demo);
        assert(memcmp(demo.ch1, original, sizeof(original)) == 0);
        demo_signal_zoom_time(&demo, 1);
        assert(demo.time_index == 11 &&
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
        assert(previous - demo.screen.zoom_window_start == 16);
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
