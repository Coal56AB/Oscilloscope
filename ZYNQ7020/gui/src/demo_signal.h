#ifndef DEMO_SIGNAL_H
#define DEMO_SIGNAL_H

#include "scope_screen.h"
#include "signal_generator.h"
#include "channel_response.h"
#include "input_range.h"
#include <stddef.h>

typedef enum {
    DEMO_ENC_CH1, DEMO_ENC_CH2, DEMO_ENC_TIME, DEMO_ENC_TRIGGER, DEMO_ENC_FUNCTION,
    DEMO_BTN_CH1, DEMO_BTN_CH2, DEMO_BTN_CURSOR, DEMO_BTN_MODE, DEMO_BTN_MENU,
    DEMO_BTN_RUN, DEMO_CONTROL_COUNT
} DemoControl;

typedef enum {
    DEMO_ACTION_NONE, DEMO_ACTION_SCREENSHOT, DEMO_ACTION_SCREEN_AND_WAVE,
    DEMO_ACTION_WAVEFORM, DEMO_ACTION_BROWSE_REFRESH, DEMO_ACTION_BROWSE_OPEN,
    DEMO_ACTION_BROWSE_CLOSE, DEMO_ACTION_BROWSE_PREV, DEMO_ACTION_BROWSE_NEXT,
    DEMO_ACTION_BROWSE_DELETE, DEMO_ACTION_PRESET_SAVE, DEMO_ACTION_PRESET_LOAD
} DemoAction;

typedef enum {
    DEMO_MENU_NONE, DEMO_MENU_MAIN, DEMO_MENU_CH1, DEMO_MENU_CH2,
    DEMO_MENU_TRIGGER, DEMO_MENU_CURSOR, DEMO_MENU_MEASURE, DEMO_MENU_DISPLAY,
    DEMO_MENU_TIME, DEMO_MENU_PROCESSING, DEMO_MENU_BROWSE_TYPE,
    DEMO_MENU_BROWSE, DEMO_MENU_PC, DEMO_MENU_DEBUG, DEMO_MENU_GENERATOR,
    DEMO_MENU_FONT, DEMO_MENU_SETUP, DEMO_MENU_PRESET, DEMO_MENU_PRESETS, DEMO_MENU_RESPONSE,
    DEMO_MENU_FILES, DEMO_MENU_LAYOUT, DEMO_MENU_CALIBRATION, DEMO_MENU_ZERO, DEMO_MENU_NOISE
} DemoMenu;

typedef enum {
    DEMO_WAVE_SQUARE=SIGNAL_WAVE_SQUARE, DEMO_WAVE_SINE=SIGNAL_WAVE_SINE,
    DEMO_WAVE_TRIANGLE=SIGNAL_WAVE_TRIANGLE, DEMO_WAVE_SAW=SIGNAL_WAVE_SAW,
    DEMO_WAVE_SINC=SIGNAL_WAVE_SINC, DEMO_WAVE_NOISE=SIGNAL_WAVE_NOISE,
    DEMO_WAVE_COUNT=SIGNAL_WAVE_COUNT
} DemoWaveShape;

#define DEMO_BROWSE_FILES 64
#define DEMO_BROWSE_NAME 32
#define DEMO_BROWSE_PAGE 7

typedef struct {
    int16_t ch1[SCOPE_PLOT_WIDTH];
    int16_t ch2[SCOPE_PLOT_WIDTH];
    int zero_y[2];
    int mv_per_div[2];
    int scale_index[2];
    int probe_ten[2];
    int enabled[2];
    int time_index;
    double time_us_per_div;
    int trigger_marker_x;
    int trigger_y;
    int trigger_source_channel;
    int trigger_edge;
} DemoWaveCapture;

typedef enum {
    DEMO_MEAS_CH1_VPP, DEMO_MEAS_CH1_RMS, DEMO_MEAS_CH1_MEAN,
    DEMO_MEAS_CH1_MIN, DEMO_MEAS_CH1_MAX,
    DEMO_MEAS_CH2_VPP, DEMO_MEAS_CH2_RMS, DEMO_MEAS_CH2_MEAN,
    DEMO_MEAS_CH2_MIN, DEMO_MEAS_CH2_MAX,
    DEMO_MEAS_CH1_FREQ, DEMO_MEAS_CH1_PERIOD, DEMO_MEAS_CH1_DUTY,
    DEMO_MEAS_CH2_FREQ, DEMO_MEAS_CH2_PERIOD, DEMO_MEAS_CH2_DUTY,
    DEMO_MEAS_CURSOR_DT, DEMO_MEAS_CURSOR_INV_DT, DEMO_MEAS_CURSOR_DV,
    DEMO_MEAS_CURSOR_A, DEMO_MEAS_CURSOR_B, DEMO_MEAS_COUNT
} DemoMeasurement;

#define DEMO_CAPTURE_POINTS 8192

typedef struct {
    ScopeScreen screen;
    int16_t ch1[SCOPE_PLOT_WIDTH];
    int16_t ch2[SCOPE_PLOT_WIDTH];
    int16_t saved_ch1[SCOPE_PLOT_WIDTH];
    int16_t saved_ch2[SCOPE_PLOT_WIDTH];
    int16_t minimum[2][SCOPE_PLOT_WIDTH];
    int16_t maximum[2][SCOPE_PLOT_WIDTH];
    DemoWaveCapture loaded_wave;
    int16_t history_ch1[8][SCOPE_PLOT_WIDTH];
    int16_t history_ch2[8][SCOPE_PLOT_WIDTH];
    int16_t history_minimum[2][8][SCOPE_PLOT_WIDTH];
    int16_t history_maximum[2][8][SCOPE_PLOT_WIDTH];
    uint8_t fft_ch1[SCOPE_FFT_BINS];
    uint8_t fft_ch2[SCOPE_FFT_BINS];
    char fft_frequency_text[SCOPE_FFT_TICKS][24];
    char fft_info_text[64];
    char fft_cursor_text[24];
    double fft_span_hz;
    unsigned fft_size;
    char ch_scale_text[2][20];
    char edit_scale_text[20];
    char ch_input_text[2][20];
    char time_text[20];
    char zoom_text[24];
    char horizontal_position_text[24];
    char trigger_level_text[20];
    char trigger_preview_text[20];
    char trigger_source_text[20];
    char measurement_value_text[SCOPE_MEASURE_SLOTS][24];
    char cursor_measurement_value_text[SCOPE_CURSOR_ROWS][SCOPE_CURSOR_READOUTS][24];
    char menu_page_text[20];
    char input_timing_text[2][20];
    char adc_level_text[2][4];
    char status_text[80];
    int scale_index[2];
    int channel_position[2];
    int coupling[2];
    int probe_ten[2];
    int time_index;
    int record_depth_index;
    double acquisition_rate_hz;
    char acquisition_rate_text[32];
    int filter_level[2];
    int intensity_coloring;
    char layout_value_text[5][16];
    double history_view[33];
    int persistence_index;
    int settings_available;
    int fft_enabled;
    int zoom_split_height;
    int fft_split_height;
    int history_count;
    int time_position;
    int position_mode[3];
    int trigger_mode_index;
    int trigger_source_index;
    int trigger_edge_index;
    int trigger_holdoff_index;
    int holdoff_ticks;
    int generator_wave[2];
    int generator_frequency_index;
    int adc_simulation, external_capture;
    int external_frame_valid;
    uint64_t external_sequence;
    int measurement_ids[SCOPE_MEASURE_SLOTS];
    int measurement_count;
    int measurement_selected;
    int measurement_menu_index;
    char browse_files[DEMO_BROWSE_FILES][DEMO_BROWSE_NAME];
    char browse_display_names[DEMO_BROWSE_PAGE][DEMO_BROWSE_NAME];
    char browser_title_text[DEMO_BROWSE_NAME];
    int browse_count;
    int browse_selected;
    int browse_filter;
    int pc_baud_index;
    int startup_preset, preset_selected;
    unsigned preset_valid;
    ChannelResponse response[4]; /* Real CH1/CH2, then independent simulation curves. */
    ChannelBaseline baseline[4][INPUT_RANGE_STAGE_COUNT];
    ChannelBaseline baseline_work[INPUT_RANGE_STAGE_COUNT];
    int baseline_active,baseline_frames;
    int baseline_stage,baseline_original_scale,calibration_scale_target,calibration_scale_channel;
    uint64_t baseline_scale_retry_ms,baseline_settle_ms;
    uint64_t baseline_seen_sequence,baseline_samples;
    double baseline_sum,baseline_power,baseline_gain;
    char baseline_text[2][64];
    int response_enabled[2],response_channel,response_active,response_sim_index;
    uint64_t response_started_ms,response_seen_sequence;
    int pc_available;
    int pc_active;
    int zoom_time_index;
    double zoom_offset;
    int saved_trigger_marker_x;
    /* STOP and ZOOM read this frozen record; view controls never regenerate it. */
    double capture_start_us, capture_step_us, source_step_us;
    size_t source_count;
    double source_samples[2][DEMO_CAPTURE_POINTS];
    double filtered_samples[2][DEMO_CAPTURE_POINTS];
    int menu_position_x;
    int menu_position_y;
    DemoMenu menu_kind;
    DemoMenu menu_parent;
    int edit_value;
    uint64_t last_channel_press_ms[2];
    uint64_t synthetic_ms;
    uint8_t channel_press_valid[2];
    int hold_time_ms;
    int double_click_ms;
    int capture_trigger_x;
    double trigger_voltage;
    double trigger_preview_voltage;
    int phase;
    uint32_t capture_sequence;
    uint64_t now_ms;
    uint64_t notice_until_ms;
    uint8_t waveform_loaded;
    uint8_t loaded_timing_valid[2];
    /* Optional device-owned input scales; callback queues an absolute request. */
    int (*request_scale)(void *context, unsigned channel, int millivolts);
    void *scale_context;
    double loaded_period_us[2];
    double loaded_duty_percent[2];
} DemoSignal;
void demo_signal_present_external(DemoSignal *demo, uint64_t sequence);
unsigned demo_signal_generator_hz(int index);

void demo_signal_init(DemoSignal *demo);
void demo_signal_refresh(DemoSignal *demo);
void demo_signal_response_observe(DemoSignal *demo,uint64_t sequence,double frequency,double amplitude);
void demo_signal_baseline_observe(DemoSignal *demo,uint64_t sequence,size_t samples,double mean,double rms,double gain);
ChannelBaseline *demo_signal_baseline(DemoSignal *demo,unsigned channel);
void demo_signal_baseline_cancel(DemoSignal *demo);
double demo_signal_adc_gain(const DemoSignal *demo,unsigned channel);
ChannelResponse *demo_signal_response_curve(DemoSignal *demo,unsigned channel);
int demo_signal_response_enable(DemoSignal *demo,unsigned channel,int enabled);
void demo_signal_set_fft_range(DemoSignal *demo, double span_hz, unsigned size);
void demo_signal_advance(DemoSignal *demo);
void demo_signal_update_time(DemoSignal *demo, uint64_t now_ms);
unsigned demo_signal_pc_baud(int index);
void demo_signal_pc_status(DemoSignal *demo, int available, unsigned baud, int active);
void demo_signal_rotate(DemoSignal *demo, DemoControl control, int steps);
int demo_signal_menu_encoder(const DemoSignal *demo,DemoControl control);
DemoAction demo_signal_press(DemoSignal *demo, DemoControl control, int long_press);
DemoAction demo_signal_press_at(DemoSignal *demo, DemoControl control,
                                int long_press, uint64_t now_ms);
void demo_signal_move_trigger(DemoSignal *demo, int delta);
void demo_signal_set_trigger_preview_y(DemoSignal *demo, int plot_y);
void demo_signal_move_channel(DemoSignal *demo, int channel, int delta);
void demo_signal_zoom_channel(DemoSignal *demo, int channel, int steps);
void demo_signal_zoom_time(DemoSignal *demo, int steps);
void demo_signal_set_zoom_center(DemoSignal *demo, int source_x);
void demo_signal_ui_open_menu(DemoSignal *demo, DemoMenu kind);
void demo_signal_ui_menu_back(DemoSignal *demo);
void demo_signal_ui_dismiss_menu(DemoSignal *demo);
void demo_signal_ui_menu_select(DemoSignal *demo, int index);
void demo_signal_ui_menu_choose(DemoSignal *demo, int index, int option);
/* Direct settings API shared by the local menus and PC frontend. */
int demo_signal_set_option(DemoSignal *demo, DemoMenu kind, int item, int value);
void demo_signal_set_trigger_voltage(DemoSignal *demo, double voltage);
void demo_signal_force_trigger(DemoSignal *demo);
double demo_signal_time_scale_us(int index);
int demo_signal_scale_mv(int index);
void demo_signal_bind_scale_control(DemoSignal *demo,
    int (*request)(void *, unsigned, int), void *context);
void demo_signal_accept_scale(DemoSignal *demo, unsigned channel, int millivolts);
/* Physical request: selected scale for an enabled channel, zero for all relays off. */
int demo_signal_panel_scale(const DemoSignal *demo, unsigned channel);
void demo_signal_ui_menu_adjust(DemoSignal *demo, int steps);
DemoAction demo_signal_ui_menu_activate(DemoSignal *demo);
DemoAction demo_signal_ui_menu_tap(DemoSignal *demo, int index, int direction);
void demo_signal_ui_toggle_run(DemoSignal *demo);
void demo_signal_ui_toggle_zoom(DemoSignal *demo);
void demo_signal_ui_toggle_fine(DemoSignal *demo);
void demo_signal_ui_apply_trigger(DemoSignal *demo);
void demo_signal_ui_pan_time(DemoSignal *demo, int delta);
void demo_signal_ui_move_cursor(DemoSignal *demo, int coordinate);
void demo_signal_ui_move_fft_cursor(DemoSignal *demo, int coordinate);
void demo_signal_ui_drag_fft_cursor(DemoSignal *demo, int coordinate, int previous,
                                    int *fine_remainder);
void demo_signal_ui_select_cursor(DemoSignal *demo, int selected);
void demo_signal_ui_cycle_cursor_mode(DemoSignal *demo);
void demo_signal_ui_cycle_trigger_mode(DemoSignal *demo);
void demo_signal_ui_reset_channel_position(DemoSignal *demo, int channel);
void demo_signal_ui_reset_time_position(DemoSignal *demo);
void demo_signal_ui_move_measurements(DemoSignal *demo, int dx, int dy);
void demo_signal_ui_move_menu(DemoSignal *demo, int dx, int dy);
void demo_signal_ui_toggle_measurement_layout(DemoSignal *demo);
void demo_signal_ui_toggle_measurements_visible(DemoSignal *demo);
void demo_signal_ui_set_split_height(DemoSignal *demo, int height);
void demo_signal_ui_set_browse_files(DemoSignal *demo, const char *const *names, int count);
const char *demo_signal_ui_browse_filename(const DemoSignal *demo);
void demo_signal_ui_request_browse_delete(DemoSignal *demo, int index);
void demo_signal_export_wave(const DemoSignal *demo, DemoWaveCapture *wave);
void demo_signal_ui_show_wave(DemoSignal *demo, const char *name,
                              const DemoWaveCapture *wave);
void demo_signal_ui_show_capture(DemoSignal *demo, const char *name,
                                 const uint32_t *pixels);
void demo_signal_ui_close_capture(DemoSignal *demo);
void demo_signal_notify(DemoSignal *demo, const char *message);
void demo_signal_begin_external(DemoSignal *demo);
int demo_signal_set_adc_level(DemoSignal *demo, const uint8_t code[2], int valid);
/* Battery voltage is already restored by the STM32 from its 1:2 divider. */
int demo_signal_set_battery(DemoSignal *demo, unsigned millivolts, int valid);

#endif
