#ifndef SCOPE_SCREEN_H
#define SCOPE_SCREEN_H

#include <stdint.h>

#define SCOPE_WIDTH 1024
#define SCOPE_HEIGHT 600
#define SCOPE_PLOT_WIDTH 1024
#define SCOPE_PLOT_HEIGHT 480
#define SCOPE_PLOT_X 0
#define SCOPE_PLOT_Y 52
#define SCOPE_BOTTOM_Y 536
#define SCOPE_BOTTOM_HEIGHT 64
#define SCOPE_MEASURE_BOTTOM_Y (SCOPE_PLOT_Y + SCOPE_PLOT_HEIGHT)
#define SCOPE_MIN_TEXT_SCALE 2
#define SCOPE_MENU_X 118
#define SCOPE_MENU_Y 82
#define SCOPE_MENU_WIDTH 788
#define SCOPE_MENU_ROW_Y 134
#define SCOPE_MENU_ROW_HEIGHT 54
#define SCOPE_MENU_CONTROL_X 450
#define SCOPE_MENU_CONTROL_WIDTH 430
#define SCOPE_MENU_CHOICES 4
#define SCOPE_MEASURE_SLOTS 10
#define SCOPE_MEASURE_CATALOG_ITEMS 16
#define SCOPE_MEASURE_CONFIRM_X 302
#define SCOPE_MEASURE_CONFIRM_Y 228
#define SCOPE_MEASURE_CONFIRM_WIDTH 420
#define SCOPE_MEASURE_CONFIRM_HEIGHT 150
#define SCOPE_BROWSE_CONFIRM_X 302
#define SCOPE_BROWSE_CONFIRM_Y 228
#define SCOPE_BROWSE_CONFIRM_WIDTH 420
#define SCOPE_BROWSE_CONFIRM_HEIGHT 150
#define SCOPE_CURSOR_READOUTS 4
#define SCOPE_ZOOM_CURSOR_BAND 33
#define SCOPE_CURSOR_STRIP_Y (SCOPE_MEASURE_BOTTOM_Y - SCOPE_ZOOM_CURSOR_BAND)
#define SCOPE_FFT_BINS 128

enum { SCOPE_CURSOR_OFF, SCOPE_CURSOR_TIME, SCOPE_CURSOR_VOLTAGE };
enum { SCOPE_FONT_PIXEL, SCOPE_FONT_INTER, SCOPE_FONT_COUNT };

#define SCOPE_MENU_ITEMS 21
#define SCOPE_MEASURE_MENU_ROW_Y 150
#define SCOPE_MEASURE_MENU_ROW_HEIGHT 44

/* Each sample is a Y coordinate within the plot: 0 is the top edge. */
typedef struct {
    const int16_t *ch1_samples; /* SCOPE_PLOT_WIDTH entries, or NULL */
    const int16_t *ch2_samples; /* SCOPE_PLOT_WIDTH entries, or NULL */
    const int16_t *ch1_min_samples, *ch1_max_samples; /* Optional min/max envelope */
    const int16_t *ch2_min_samples, *ch2_max_samples;
    const int16_t *capture_ch1_samples; /* Full capture while zoomed */
    const int16_t *capture_ch2_samples;
    const int16_t *history_ch1_samples; /* history_count frames, each SCOPE_PLOT_WIDTH entries */
    const int16_t *history_ch2_samples;
    int history_count;
    const char *ch1_scale;
    const char *ch2_scale;
    const char *ch1_input;
    const char *ch2_input;
    const char *time_scale;
    const char *horizontal_position;
    const char *trigger_mode;
    const char *trigger_source;
    const char *trigger_level;
    const char *trigger_preview_level;
    const char *measurement_labels[SCOPE_MEASURE_SLOTS];
    const char *measurement_values[SCOPE_MEASURE_SLOTS];
    int measurement_count;
    const char *cursor_measurement_labels[SCOPE_CURSOR_READOUTS];
    const char *cursor_measurement_values[SCOPE_CURSOR_READOUTS];
    int cursor_measurement_count;
    uint8_t fine_mode;
    int measurement_x;
    int measurement_y;
    int menu_x;
    int menu_y;
    uint8_t measurement_horizontal;
    uint8_t measurement_hidden;
    const char *status_message;
    uint8_t status_visible;
    uint8_t status_warning;
    uint8_t status_alpha;
    const char *browser_title;
    const uint32_t *browser_pixels; /* SCOPE_WIDTH * SCOPE_HEIGHT RGB888 words */
    uint8_t browser_visible;
    uint8_t waveform_loaded;
    const uint8_t *fft_ch1_bins;
    const uint8_t *fft_ch2_bins;
    const char *fft_span;
    int split_height; /* Upper overview or FFT, measured from SCOPE_PLOT_Y */
    uint8_t fft_enabled;
    uint8_t font_index;
    uint8_t ui_rounding;
    const char *menu_title;
    const char *menu_page;
    const char *menu_labels[SCOPE_MENU_ITEMS];
    const char *menu_values[SCOPE_MENU_ITEMS];
    const char *const *menu_options[SCOPE_MENU_ITEMS];
    uint8_t menu_option_count[SCOPE_MENU_ITEMS];
    uint8_t menu_option_selected[SCOPE_MENU_ITEMS];
    uint8_t menu_stepper[SCOPE_MENU_ITEMS];
    int trigger_y;             /* Plot coordinate, 0..SCOPE_PLOT_HEIGHT-1 */
    int trigger_preview_y;
    int cursor_a;
    int cursor_b;
    int menu_selected;
    int menu_count;
    int trigger_marker_x;
    int zoom_window_start;
    int zoom_window_end;
    int channel_zero_y[2];     /* Ground reference of each channel in plot coordinates */
    uint8_t trigger_preview;
    uint8_t intensity_coloring;
    uint8_t trigger_source_channel;
    uint8_t trigger_edge_falling;
    uint8_t trigger_edge_both;
    uint8_t cursor_source_channel;
    uint8_t battery_known;
    uint8_t battery_percent;
    uint8_t battery_charging;
    uint8_t cursor_mode;
    uint8_t cursor_selected;
    uint8_t menu_open;
    uint8_t menu_editing;
    uint8_t measurement_menu;
    uint8_t measurement_clear_confirm;
    uint8_t measurement_clear_choice;
    uint8_t browser_delete_confirm;
    uint8_t browser_delete_choice;
    uint8_t grid_enabled;
    uint8_t zoom_enabled;
    uint8_t ch1_enabled;
    uint8_t ch2_enabled;
    uint8_t ch_position_mode[2];
    uint8_t time_position_mode;
    uint8_t running;
    uint8_t waiting_for_trigger;
    uint8_t trigger_locked;
} ScopeScreen;

/* Pixels use 0x00RRGGBB, with stride measured in pixels. */
void scope_screen_render(uint32_t *pixels, int stride, const ScopeScreen *screen);
int scope_screen_cursor_measurement_width(const ScopeScreen *screen);
void scope_screen_measurement_bounds(const ScopeScreen *screen, int *x, int *y,
                                     int *width, int *height);

#endif
