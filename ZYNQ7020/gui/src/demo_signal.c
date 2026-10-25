#include "demo_signal.h"
#include "capture_settings.h"
#include "signal_generator.h"
#include "signal_filter.h"
#include "spectrum_bins.h"
#include "dsp_fft.h"
#include "battery_level.h"
#include "waveform_view.h"
#include "control_protocol.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define PLOT_MIDDLE (SCOPE_PLOT_HEIGHT / 2)
#define PIXELS_PER_DIV (SCOPE_PLOT_HEIGHT / 8.0)
/* Maximum simulated acquisition rate: 200 MSPS. */
#define GENERATOR_MIN_SAMPLE_US 0.005

#ifndef DEMO_ENABLE_GENERATOR
#define DEMO_ENABLE_GENERATOR 1
#endif

static const double time_us[] = {
    0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
    10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000
};
double demo_signal_time_scale_us(int index)
{
    return index>=0&&index<(int)(sizeof(time_us)/sizeof(time_us[0]))?time_us[index]:0;
}
int demo_signal_scale_mv(int index)
{
    return index >= 0 ? control_scale_mv((unsigned)index) : 0;
}
#define TIME_SCALE_COUNT ((int)(sizeof(time_us) / sizeof(time_us[0])))
#define DEFAULT_TIME_INDEX 12
static const char *coupling_names[] = {"DC", "AC"};
static const char *probe_names[] = {"x1", "x10"};
static const char *channel_names[] = {"CH1", "CH2"};
static const char *on_off_names[] = {"OFF", "ON"};
static const char *cursor_mode_names[] = {"OFF", "TIME", "VERTICAL"};
static const char *font_names[] = {"PIXEL", "INTER"};
static const char *mode_names[] = {"AUTO", "NORMAL", "SINGLE"};
static const char *edge_names[] = {"RISING", "FALLING", "BOTH"};
static const char *holdoff_names[] = {"OFF", "10 us", "50 us", "100 us"};
static const char *generator_wave_names[] = {
    "SQUARE", "SINE", "TRIANGLE", "SAW", "SINC", "NOISE"
};
static const char *generator_wave_options[] = {
    "SQR", "SINE", "TRI", "SAW", "SINC", "NOISE"
};
static const char *generator_frequency_names[] = {
    "1 Hz", "2 Hz", "5 Hz", "10 Hz", "20 Hz", "50 Hz", "100 Hz", "200 Hz", "500 Hz",
    "1 kHz", "2 kHz", "5 kHz", "10 kHz", "20 kHz", "50 kHz",
    "100 kHz", "200 kHz", "500 kHz", "1 MHz"
};
static const double generator_period_us[] = {
    1000000.0, 500000.0, 200000.0, 100000.0, 50000.0, 20000.0, 10000.0, 5000.0, 2000.0,
    1000.0, 500.0, 200.0, 100.0, 50.0, 20.0, 10.0, 5.0, 2.0, 1.0
};
unsigned demo_signal_generator_hz(int index)
{
    return index >= 0 && index < (int)(sizeof(generator_period_us)/sizeof(generator_period_us[0])) ?
           (unsigned)(1000000.0/generator_period_us[index]) : 0;
}
static const struct {
    const char *label;
    DemoMenu target;
} main_items[] = {
    {"SETUP", DEMO_MENU_SETUP},
    {"BROWSE", DEMO_MENU_BROWSE_TYPE},
    {"DISPLAY", DEMO_MENU_DISPLAY},
    {"MEASUREMENTS", DEMO_MENU_MEASURE},
    {"PROCESSING", DEMO_MENU_PROCESSING},
    {"FILES", DEMO_MENU_FILES},
#if DEMO_ENABLE_GENERATOR
    {"DEBUG", DEMO_MENU_DEBUG},
#endif
};
static const char *debug_labels[] = {"GENERATOR", "LONG PRESS", "DOUBLE CLICK", "SIMULATION", "ADC CH1", "ADC CH2"};
static const char *generator_labels[] = {"CH1 WAVE", "CH2 WAVE", "FREQUENCY"};
static const char *browse_type_labels[] = {"BMP IMAGES", "CSV WAVES"};
static const char *channel_labels[] = {"CHANNEL", "COUPLING", "PROBE", "V / DIV", "CALIBRATE"};
static const char *trigger_labels[] = {
    "MODE", "SOURCE", "EDGE", "HOLDOFF", "FORCE", "RESET LEVEL", "LEVEL"
};
static const char *cursor_labels[] = {"MODE"};
static const char *pc_labels[] = {"UART BAUD", "STATUS"};
static const char *pc_baud_names[] = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "1000000", "2000000"};
static const unsigned pc_bauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000};
#define PC_BAUD_COUNT ((int)(sizeof(pc_bauds) / sizeof(pc_bauds[0])))

unsigned demo_signal_pc_baud(int index)
{
    return index >= 0 && index < PC_BAUD_COUNT ? pc_bauds[index] : 0;
}
static const char *time_labels[] = {"TIME / DIV", "RESET POSITION", "SAMPLE RATE"};
static const char *record_names[] = {"10 K", "50 K", "100 K", "500 K", "1 M"};
static const char *processing_labels[] = {
    "CH1 FILTER", "CH2 FILTER", "COLORING", "PERSISTENCE", "FFT", "RECORD LENGTH"
};
static const char *display_labels[] = {"GRID", "FONT"};
static const char *filter_names[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
static const char *startup_names[] = {"LAST", "1", "2", "3", "4", "5"};
static const char *preset_names[] = {"PRESET 1", "PRESET 2", "PRESET 3", "PRESET 4", "PRESET 5"};
static const char *persistence_names[] = {"1 SWEEP", "4 SWEEPS", "8 SWEEPS"};
static const int persistence_depth[] = {1, 4, 8};
static const char *measurement_labels[DEMO_MEAS_COUNT] = {
    "CH1 VPP", "CH1 RMS", "CH1 MEAN", "CH1 MIN", "CH1 MAX",
    "CH2 VPP", "CH2 RMS", "CH2 MEAN", "CH2 MIN", "CH2 MAX",
    "CH1 FREQ", "CH1 PERIOD", "CH1 DUTY",
    "CH2 FREQ", "CH2 PERIOD", "CH2 DUTY",
    "CURSOR DT", "CURSOR 1/DT", "CURSOR DV", "CURSOR A", "CURSOR B"
};
static const int channel_measure_ids[] = {
    DEMO_MEAS_CH1_VPP, DEMO_MEAS_CH1_RMS, DEMO_MEAS_CH1_MEAN,
    DEMO_MEAS_CH1_MIN, DEMO_MEAS_CH1_MAX, DEMO_MEAS_CH1_FREQ,
    DEMO_MEAS_CH1_PERIOD, DEMO_MEAS_CH1_DUTY,
    DEMO_MEAS_CH2_VPP, DEMO_MEAS_CH2_RMS, DEMO_MEAS_CH2_MEAN,
    DEMO_MEAS_CH2_MIN, DEMO_MEAS_CH2_MAX, DEMO_MEAS_CH2_FREQ,
    DEMO_MEAS_CH2_PERIOD, DEMO_MEAS_CH2_DUTY
};
_Static_assert(sizeof(channel_measure_ids) / sizeof(channel_measure_ids[0]) ==
               SCOPE_MEASURE_CATALOG_ITEMS, "measurement catalog size mismatch");

static int clamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void uppercase_filename(char destination[DEMO_BROWSE_NAME], const char *source)
{
    size_t i;
    for (i = 0; i + 1 < DEMO_BROWSE_NAME && source[i]; ++i)
        destination[i] = (char)toupper((unsigned char)source[i]);
    destination[i] = '\0';
}

static double volts_per_div(const DemoSignal *demo, int channel)
{
    return demo_signal_scale_mv(demo->scale_index[channel]) *
           (demo->probe_ten[channel] ? 10.0 : 1.0) / 1000.0;
}

static void format_scale(char *buffer, size_t size, int scale_index, int probe_ten)
{
    int mv = demo_signal_scale_mv(scale_index) * (probe_ten ? 10 : 1);
    if (mv < 1000) snprintf(buffer, size, "%d mV", mv);
    else snprintf(buffer, size, "%d V", mv / 1000);
}

static const char *time_unit(double microseconds, double *divisor)
{
    double magnitude = fabs(microseconds);
    if (magnitude > 0.0 && magnitude < 1.0) { *divisor = 0.001; return "ns"; }
    if (magnitude < 1000.0) { *divisor = 1.0; return "us"; }
    if (magnitude < 1000000.0) { *divisor = 1000.0; return "ms"; }
    *divisor = 1000000.0; return "s";
}

static void format_time_value(char *buffer, size_t size, double microseconds)
{
    double divisor;
    const char *unit = time_unit(microseconds, &divisor);
    snprintf(buffer, size, "%.3g %s", microseconds / divisor, unit);
}

static void format_time_scale(char *buffer, size_t size, int index)
{
    format_time_value(buffer, size, time_us[index]);
}

static void format_cursor_number(char *buffer, size_t size, double value,
                                  const char *unit, int sign)
{
    int decimals = value == 0.0 ? 3 : 3 - (int)floor(log10(fabs(value)));
    decimals = clamp(decimals, 0, 3);
    if (decimals > 0 && fabs(value) + 0.5 * pow(10.0, -decimals) >= pow(10.0, 4 - decimals))
        --decimals;
    snprintf(buffer, size, sign ? "%+.*f %s" : "%.*f %s", decimals, value, unit);
}

static void format_cursor_time(char *buffer, size_t size, double microseconds, int sign)
{
    double divisor;
    const char *unit = time_unit(microseconds, &divisor);
    format_cursor_number(buffer, size, microseconds / divisor, unit, sign);
}

static void format_frequency(char *text, size_t size, double hz)
{
    const char *unit = hz >= 1000000.0 ? "M" : hz >= 1000.0 ? "k" : "";
    double divisor = hz >= 1000000.0 ? 1000000.0 : hz >= 1000.0 ? 1000.0 : 1.0;
    snprintf(text, size, "%.4g%s", hz / divisor, unit);
}

static void update_fft_cursor(DemoSignal *demo)
{
    double hz = spectrum_frequency_at((double)demo->screen.fft_cursor_x / (SCOPE_PLOT_WIDTH - 1),
                                      demo->fft_span_hz, demo->fft_size);
    double divisor = hz >= 1000000.0 ? 1000000.0 : hz >= 1000.0 ? 1000.0 : 1.0;
    const char *unit = hz >= 1000000.0 ? "MHz" : hz >= 1000.0 ? "kHz" : "Hz";
    if (demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE)
        format_cursor_number(demo->fft_cursor_text, sizeof(demo->fft_cursor_text),
                             spectrum_db_at_level(demo->screen.fft_cursor_level), "dB", 0);
    else if (hz == 0.0) snprintf(demo->fft_cursor_text, sizeof(demo->fft_cursor_text), "0 Hz");
    else format_cursor_number(demo->fft_cursor_text, sizeof(demo->fft_cursor_text), hz / divisor, unit, 0);
    demo->screen.fft_cursor_value = demo->fft_cursor_text;
}

void demo_signal_ui_move_fft_cursor(DemoSignal *demo, int coordinate)
{
    if (!demo->fft_enabled || demo->screen.cursor_mode == SCOPE_CURSOR_OFF) return;
    if (demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE)
        demo->screen.fft_cursor_level = clamp(coordinate, 0, 255);
    else demo->screen.fft_cursor_x = clamp(coordinate, 0, SCOPE_PLOT_WIDTH - 1);
    demo->screen.cursor_selected = SCOPE_CURSOR_SELECT_FFT;
    update_fft_cursor(demo);
}

void demo_signal_ui_drag_fft_cursor(DemoSignal *demo, int coordinate, int previous,
                                    int *fine_remainder)
{
    if (demo->screen.fine_mode) {
        int delta = coordinate - previous + *fine_remainder;
        *fine_remainder = delta % 4;
        coordinate = (demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ?
                      demo->screen.fft_cursor_level : demo->screen.fft_cursor_x) + delta / 4;
    } else *fine_remainder = 0;
    demo_signal_ui_move_fft_cursor(demo, coordinate);
}

void demo_signal_set_fft_range(DemoSignal *demo, double span_hz, unsigned size)
{
    char sampling[24], step[24];
    int tick;
    demo->fft_span_hz = span_hz;
    demo->fft_size = size ? size : SCOPE_PLOT_WIDTH;
    update_fft_cursor(demo);
    format_frequency(sampling, sizeof(sampling), span_hz * 2.0);
    format_frequency(step, sizeof(step), span_hz * 2.0 / demo->fft_size);
    snprintf(demo->fft_info_text, sizeof(demo->fft_info_text), "FS %s / DF %s", sampling, step);
    demo->screen.fft_sampling_info = demo->fft_info_text;
    for (tick = 0; tick < SCOPE_FFT_TICKS; ++tick) {
        double hz = spectrum_frequency_at((double)tick / (SCOPE_FFT_TICKS - 1), span_hz, demo->fft_size);
        const char *unit = hz >= 1000000.0 ? "M" : hz >= 1000.0 ? "k" : "";
        double divisor = hz >= 1000000.0 ? 1000000.0 : hz >= 1000.0 ? 1000.0 : 1.0;
        snprintf(demo->fft_frequency_text[tick], sizeof(demo->fft_frequency_text[tick]),
                      "%.3g%s", hz / divisor, unit);
        demo->screen.fft_frequency_labels[tick] = demo->fft_frequency_text[tick];
    }
}

static double zoom_ratio(const DemoSignal *demo)
{
    return demo->screen.zoom_enabled ? time_us[demo->time_index] / time_us[demo->zoom_time_index] : 1.0;
}

static double current_time_us_per_div(const DemoSignal *demo)
{
    int index = demo->screen.zoom_enabled ? demo->zoom_time_index : demo->time_index;
    if (demo->waveform_loaded)
        return demo->loaded_wave.time_us_per_div * time_us[index] /
               time_us[demo->loaded_wave.time_index];
    return time_us[index];
}

static double clamp_position(double value, double limit)
{
    return fmax(-limit, fmin(value, limit));
}

static double loaded_view_step(const DemoSignal *demo)
{
    return current_time_us_per_div(demo) / demo->loaded_wave.time_us_per_div;
}

static int loaded_pan_limit(const DemoSignal *demo)
{
    double step = loaded_view_step(demo);
    return (int)ceil(SCOPE_PLOT_WIDTH * fabs(1.0 - step) / (2.0 * step));
}


static int channel_zero(const DemoSignal *demo, int channel)
{
    return (channel ? 330 : 200) + demo->channel_position[channel];
}

static int centered_channel_position(int channel)
{
    return PLOT_MIDDLE - (channel ? 330 : 200);
}

static int source_zero(const DemoSignal *demo)
{
    return channel_zero(demo, demo->trigger_source_index);
}

static void update_trigger_positions(DemoSignal *demo)
{
    double pixels_per_volt = PIXELS_PER_DIV /
                            volts_per_div(demo, demo->trigger_source_index);
    int zero = source_zero(demo);
    demo->screen.trigger_y = clamp((int)lround(zero - demo->trigger_voltage * pixels_per_volt),
                                   8, SCOPE_PLOT_HEIGHT - 9);
    demo->screen.trigger_preview_y = clamp(
        (int)lround(zero - demo->trigger_preview_voltage * pixels_per_volt),
        8, SCOPE_PLOT_HEIGHT - 9);
}

static void bind_envelopes(DemoSignal *demo)
{
    demo->screen.ch1_min_samples = demo->minimum[0];
    demo->screen.ch1_max_samples = demo->maximum[0];
    demo->screen.ch2_min_samples = demo->minimum[1];
    demo->screen.ch2_max_samples = demo->maximum[1];
}

static int trigger_fraction(const DemoSignal *demo, double *fraction)
{
    int channel = demo->trigger_source_index;
    double amplitude = channel ? 1.60 : 1.20;
    if (!(channel ? demo->screen.ch2_enabled : demo->screen.ch1_enabled)) return 0;
    *fraction = demo->trigger_voltage / amplitude +
                (demo->coupling[channel] == 1 ? 0.5 : 0.0);
    return *fraction > 0.01 && *fraction < 0.99;
}

static int find_trigger_crossing(const DemoSignal *demo, int falling,
                                 double *crossing_us)
{
    int channel = demo->trigger_source_index;
    DemoWaveShape shape = (DemoWaveShape)demo->generator_wave[channel];
    double fraction, period = generator_period_us[demo->generator_frequency_index];
    double offset = channel ? period * 0.2 : 0.0;
    double previous;
    int i;
    if (shape == DEMO_WAVE_NOISE || !trigger_fraction(demo, &fraction)) return 0;
    previous = generator_value(shape, offset, period, 0, 0, channel);
    for (i = 1; i <= 4096; ++i) {
        double t = period * i / 4096.0;
        double current = generator_value(shape, t + offset, period, 0, i, channel);
        int crossed = falling ? previous >= fraction && current < fraction :
                                previous <= fraction && current > fraction;
        if (crossed) {
            double denominator = current - previous;
            double portion = denominator == 0.0 ? 0.0 :
                             (fraction - previous) / denominator;
            *crossing_us = period * (i - 1 + portion) / 4096.0;
            return 1;
        }
        previous = current;
    }
    return 0;
}

static int trigger_crossing_available(const DemoSignal *demo)
{
    double crossing_us;
    int falling = demo->trigger_edge_index == 1 ||
                  (demo->trigger_edge_index == 2 && (demo->capture_sequence & 1));
    return find_trigger_crossing(demo, falling, &crossing_us);
}

static int sample_noise(uint32_t sequence, size_t x, int channel)
{
    return (int)(generator_noise_code(sequence, x, channel) % 7u) - 3;
}

static void filter_samples(int16_t *samples, int level)
{
    double source[SCOPE_PLOT_WIDTH], filtered[SCOPE_PLOT_WIDTH];
    int x;
    if (!level) return;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        source[x] = samples[x] == WAVEFORM_NO_SAMPLE ? NAN : samples[x];
    signal_filter_apply(source, SCOPE_PLOT_WIDTH, (unsigned)level, filtered);
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        samples[x] = isfinite(filtered[x]) ? (int16_t)lround(filtered[x]) : WAVEFORM_NO_SAMPLE;
}

static void spectrum(const int16_t *samples, uint8_t *bins)
{
    double real[SCOPE_PLOT_WIDTH], imag[SCOPE_PLOT_WIDTH];
    double peak = 0.0;
    int i, count = 0;
    double mean = 0.0;
    for (i = 0; i < SCOPE_PLOT_WIDTH; ++i)
        if (samples[i] != WAVEFORM_NO_SAMPLE) {mean += samples[i]; ++count;}
    if (count) mean /= count;
    for (i = 0; i < SCOPE_PLOT_WIDTH; ++i) {
        double window = 0.5 - 0.5 * cos(2.0 * PI * i / (SCOPE_PLOT_WIDTH - 1));
        real[i] = samples[i] == WAVEFORM_NO_SAMPLE ? 0.0 : (samples[i] - mean) * window;
        imag[i] = 0.0;
    }
    dsp_fft(real,imag,SCOPE_PLOT_WIDTH,0);
    for (i = 1; i <= SCOPE_PLOT_WIDTH / 2; ++i) {
        double magnitude = hypot(real[i], imag[i]);
        if (magnitude > peak) peak = magnitude;
    }
    real[0]=imag[0]=0;
    for (i = 0; i < SCOPE_FFT_BINS; ++i) {
        double magnitude=spectrum_project(real,imag,SCOPE_PLOT_WIDTH,(unsigned)i,SCOPE_FFT_BINS),ratio;
        ratio = peak > 0.0 ? spectrum_level_at_db(20.0 * log10(magnitude / peak + 1e-12)) : 0.0;
        bins[i] = (uint8_t)clamp((int)lround(ratio), 0, 255);
    }
}

static void update_spectrum(DemoSignal *demo)
{
    if (!demo->fft_enabled) return;
    spectrum(demo->ch1, demo->fft_ch1);
    spectrum(demo->ch2, demo->fft_ch2);
}

static void generator_source_range(const void *context, size_t first, size_t end,
                                   unsigned channel, double *minimum, double *maximum)
{
    const DemoSignal *demo = context;
    const double *values = (demo->filter_level[channel] || demo->response_enabled[channel]) ? demo->filtered_samples[channel] : demo->source_samples[channel];
    size_t i;
    *minimum = *maximum = values[first];
    for (i = first + 1; i < end; ++i) {
        if (values[i] < *minimum) *minimum = values[i];
        if (values[i] > *maximum) *maximum = values[i];
    }
}

/* Both demo and ADC records reduce their source interval through waveform_view. */
static void density_source_range(const void *context, size_t first, size_t end,
                                unsigned channel, double *minimum, double *maximum)
{
    const double *values = context;
    size_t i;
    (void)channel;
    *minimum = *maximum = values[first];
    for (i = first + 1; i < end; ++i) {
        if (values[i] < *minimum) *minimum = values[i];
        if (values[i] > *maximum) *maximum = values[i];
    }
}

static void render_generator_view(DemoSignal *demo, double start_us, double us_per_pixel)
{
    WaveformSource source={0};
    int x;
    source.context = demo;
    source.samples = demo->source_count;
    source.range = generator_source_range;
    for (x = 0; x < 2; ++x) {
        double scale = PIXELS_PER_DIV / volts_per_div(demo, x);
        double zero = channel_zero(demo, x) +
                      (demo->coupling[x] ? (x ? 1.60 : 1.20) * scale * 0.5 : 0.0);
        demo->screen.density_levels[x] = NULL;
        demo->screen.connections[x] = NULL;
        demo->screen.peak_detect = 0;
        if(demo->response_enabled[x]){
            double *work=malloc(demo->source_count*sizeof(double));
            memcpy(demo->filtered_samples[x],demo->source_samples[x],demo->source_count*sizeof(double));
            if(work){
                memcpy(work,demo->source_samples[x],demo->source_count*sizeof(double));
                channel_response_correct(work,demo->source_count,1e6/demo->source_step_us,demo_signal_response_curve(demo,(unsigned)x));
                signal_filter_apply(work,demo->source_count,(unsigned)demo->filter_level[x],demo->filtered_samples[x]);
                free(work);
            }
        }else if (demo->filter_level[x])
            signal_filter_apply(demo->source_samples[x], demo->source_count,
                                (unsigned)demo->filter_level[x], demo->filtered_samples[x]);
        demo->screen.density_source[x].context = (demo->filter_level[x] || demo->response_enabled[x]) ? demo->filtered_samples[x] : demo->source_samples[x];
        demo->screen.density_source[x].samples = demo->source_count;
        demo->screen.density_source[x].range = density_source_range;
        demo->screen.density_first = (start_us-demo->capture_start_us)/demo->source_step_us;
        demo->screen.density_samples = SCOPE_PLOT_WIDTH*us_per_pixel/demo->source_step_us;
        demo->screen.density_zero[x] = zero;
        demo->screen.density_scale[x] = scale;
        waveform_view_reduce(&source, (start_us - demo->capture_start_us) / demo->source_step_us,
                             SCOPE_PLOT_WIDTH * us_per_pixel / demo->source_step_us,
                             x, zero, scale, SCOPE_PLOT_WIDTH, x ? demo->ch2 : demo->ch1,
                             demo->minimum[x], demo->maximum[x]);
    }
    bind_envelopes(demo);
}

static void generate(DemoSignal *demo, int free_run)
{
    double step = time_us[demo->time_index] * 10.0 / SCOPE_PLOT_WIDTH;
    int marker = clamp(SCOPE_PLOT_WIDTH / 2 - demo->time_position, 0, SCOPE_PLOT_WIDTH - 1);
    int falling = demo->trigger_edge_index == 1 ||
                  (demo->trigger_edge_index == 2 && (demo->capture_sequence & 1));
    double crossing_us, duration = step * SCOPE_PLOT_WIDTH;
    double period = generator_period_us[demo->generator_frequency_index];
    double samples = ceil(duration / GENERATOR_MIN_SAMPLE_US);
    size_t i;
    unsigned channel;
    demo->screen.trigger_marker_x = marker;
    demo->capture_trigger_x = marker;
    demo->screen.trigger_locked =
        (uint8_t)(!free_run && find_trigger_crossing(demo, falling, &crossing_us));
    demo->capture_step_us = step;
    demo->capture_start_us = demo->screen.trigger_locked ? crossing_us - marker * step :
                            (demo->phase + demo->time_position) * step;
    demo->source_count = samples > DEMO_CAPTURE_POINTS ? DEMO_CAPTURE_POINTS : (size_t)samples;
    demo->source_step_us = duration / demo->source_count;
    for (channel = 0; channel < 2; ++channel) {
        double amplitude = channel ? 1.60 : 1.20;
        if(demo->adc_simulation && demo->response[channel+2].count)
            amplitude*=channel_response_demo_gain(channel,demo_signal_generator_hz(demo->generator_frequency_index));
        double noise_scale = channel ? 1.0 / 60.0 : 1.0 / 120.0;
        for (i = 0; i < demo->source_count; ++i) {
            double t = demo->capture_start_us + i * demo->source_step_us;
            demo->source_samples[channel][i] = amplitude *
                generator_value((DemoWaveShape)demo->generator_wave[channel],
                    t + (channel ? period * 0.2 : 0.0), period,
                    demo->capture_sequence, i, channel) -
                sample_noise(demo->capture_sequence, i, channel) * noise_scale;
        }
    }
    render_generator_view(demo, demo->capture_start_us, step);
}

static void save_capture(DemoSignal *demo)
{
    memcpy(demo->saved_ch1, demo->ch1, sizeof(demo->ch1));
    memcpy(demo->saved_ch2, demo->ch2, sizeof(demo->ch2));
    demo->saved_trigger_marker_x = demo->screen.trigger_marker_x;
}

static void record_history(DemoSignal *demo)
{
    int i, channel;
    const double view[]={demo->scale_index[0],demo->scale_index[1],demo->channel_position[0],demo->channel_position[1],
        demo->probe_ten[0],demo->probe_ten[1],demo->coupling[0],demo->coupling[1],demo->time_index,demo->time_position,
        demo->zoom_time_index,demo->zoom_offset,demo->screen.zoom_enabled,demo->fft_enabled,demo->fft_split_height,
        demo->zoom_split_height,demo->filter_level[0],demo->filter_level[1],demo->intensity_coloring,
        demo->response_enabled[0],demo->response_enabled[1],demo->adc_simulation,demo->acquisition_rate_hz};
    if(memcmp(demo->history_view,view,sizeof(view))){demo->history_count=0;memcpy(demo->history_view,view,sizeof(view));}
    for (i = 7; i > 0; --i) {
        memcpy(demo->history_ch1[i], demo->history_ch1[i - 1], sizeof(demo->ch1));
        memcpy(demo->history_ch2[i], demo->history_ch2[i - 1], sizeof(demo->ch2));
        for (channel = 0; channel < 2; ++channel) {
            memcpy(demo->history_minimum[channel][i], demo->history_minimum[channel][i - 1],
                   sizeof(demo->minimum[channel]));
            memcpy(demo->history_maximum[channel][i], demo->history_maximum[channel][i - 1],
                   sizeof(demo->maximum[channel]));
        }
    }
    memcpy(demo->history_ch1[0], demo->ch1, sizeof(demo->ch1));
    memcpy(demo->history_ch2[0], demo->ch2, sizeof(demo->ch2));
    for (channel = 0; channel < 2; ++channel) {
        memcpy(demo->history_minimum[channel][0], demo->minimum[channel], sizeof(demo->minimum[channel]));
        memcpy(demo->history_maximum[channel][0], demo->maximum[channel], sizeof(demo->maximum[channel]));
    }
    if (demo->history_count < 8) ++demo->history_count;
    demo->screen.history_count = demo->history_count <
        persistence_depth[demo->persistence_index] ? demo->history_count :
        persistence_depth[demo->persistence_index];
}

static void update_measurements(DemoSignal *demo);

void demo_signal_begin_external(DemoSignal *demo)
{
    int x, ch;
    demo->external_capture = 1;
    demo->external_frame_valid = 0;
    demo->screen.peak_detect = 0;
    for (ch=0;ch<2;++ch) {
        demo->screen.density_levels[ch]=NULL;
        demo->screen.connections[ch]=NULL;
        demo->screen.density_source[ch].samples=0;
        demo->screen.peak_min_samples[ch]=demo->screen.peak_max_samples[ch]=NULL;
        demo->screen.peak_strength[ch]=NULL;
    }
    demo->screen.trigger_locked = 0;
    demo->history_count = demo->screen.history_count = 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        demo->ch1[x] = demo->ch2[x] = WAVEFORM_NO_SAMPLE;
        demo->saved_ch1[x] = demo->saved_ch2[x] = WAVEFORM_NO_SAMPLE;
        for (ch = 0; ch < 2; ++ch)
            demo->minimum[ch][x] = demo->maximum[ch][x] = WAVEFORM_NO_SAMPLE;
    }
    memset(demo->fft_ch1, 0, sizeof(demo->fft_ch1));
    memset(demo->fft_ch2, 0, sizeof(demo->fft_ch2));
    bind_envelopes(demo);
    update_measurements(demo);
}

void demo_signal_present_external(DemoSignal *demo, uint64_t sequence)
{
    bind_envelopes(demo);
    if (!demo->screen.running) demo->history_count = demo->screen.history_count = 0;
    if (!demo->external_frame_valid || demo->external_sequence != sequence) {
        record_history(demo);
        demo->external_sequence = sequence;
        demo->external_frame_valid = 1;
    }
}
static void apply_zoom_view(DemoSignal *demo)
{
    double ratio = zoom_ratio(demo);
    double center = SCOPE_PLOT_WIDTH / 2.0 + demo->zoom_offset;
    double width = SCOPE_PLOT_WIDTH / ratio;
    double start = fmax(0.0, fmin(center - width / 2, SCOPE_PLOT_WIDTH - width));
    demo->screen.zoom_window_start = (int)floor(start);
    demo->screen.zoom_window_end = clamp((int)ceil(start + width), 0, SCOPE_PLOT_WIDTH);
    {
        double base_step = time_us[demo->time_index] * 10.0 / SCOPE_PLOT_WIDTH;
        double base_start = demo->capture_start_us +
            (demo->capture_trigger_x - demo->saved_trigger_marker_x) * demo->capture_step_us +
            demo->saved_trigger_marker_x * (demo->capture_step_us - base_step);
        render_generator_view(demo, base_start + start * base_step, base_step / ratio);
    }
    demo->screen.trigger_marker_x = clamp((int)lround(
        (demo->saved_trigger_marker_x - start) * ratio), 0, SCOPE_PLOT_WIDTH - 1);
}

static void loaded_source_range(const void *context, size_t first, size_t end,
                                unsigned channel, double *minimum, double *maximum)
{
    const DemoSignal *demo = context;
    const int16_t *values = channel ? demo->saved_ch2 : demo->saved_ch1;
    size_t i;
    *minimum = *maximum = NAN;
    for (i = first; i < end; ++i) {
        double value = -values[i];
        if (values[i] == WAVEFORM_NO_SAMPLE) continue;
        if (!isfinite(*minimum) || value < *minimum) *minimum = value;
        if (!isfinite(*maximum) || value > *maximum) *maximum = value;
    }
}

static void apply_loaded_view(DemoSignal *demo)
{
    const DemoWaveCapture *wave = &demo->loaded_wave;
    WaveformSource source = {demo, SCOPE_PLOT_WIDTH, loaded_source_range};
    double step = loaded_view_step(demo);
    double width, start, max_start;
    int channel, x;
    width = SCOPE_PLOT_WIDTH * step;
    max_start = SCOPE_PLOT_WIDTH - width;
    start = max_start / 2 +
            (demo->screen.zoom_enabled ? demo->zoom_offset : -demo->time_position) *
            step;
    start = fmax(fmin(0.0, max_start), fmin(start, fmax(0.0, max_start)));
    for (channel = 0; channel < 2; ++channel) {
        const int16_t *original = channel ? wave->ch2 : wave->ch1;
        int16_t *full = channel ? demo->saved_ch2 : demo->saved_ch1;
        int16_t *visible = channel ? demo->ch2 : demo->ch1;
        double ratio = (double)wave->mv_per_div[channel] /
                       (volts_per_div(demo, channel) * 1000.0);
        for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
            full[x] = original[x] == WAVEFORM_NO_SAMPLE ? WAVEFORM_NO_SAMPLE :
                       (int16_t)clamp((int)lround(channel_zero(demo, channel) +
                       (original[x] - wave->zero_y[channel]) * ratio),
                       INT16_MIN + 1, INT16_MAX);
        filter_samples(full, demo->filter_level[channel]);
        waveform_view_reduce(&source, start, width, channel, 0.0, 1.0, SCOPE_PLOT_WIDTH,
                             visible, demo->minimum[channel], demo->maximum[channel]);
    }
    demo->screen.zoom_window_start = (int)start;
    demo->screen.zoom_window_end = (int)(start + width);
    demo->screen.trigger_marker_x = clamp((int)lround(
        (wave->trigger_marker_x - start) / step), 0, SCOPE_PLOT_WIDTH - 1);
    demo->screen.trigger_locked = 1;
    bind_envelopes(demo);
}

static void analyze_loaded_timing(DemoSignal *demo, int channel)
{
    const int16_t *samples = channel ? demo->loaded_wave.ch2 : demo->loaded_wave.ch1;
    int minimum = INT16_MAX, maximum = INT16_MIN;
    int threshold, first_rise = -1, second_rise = -1, first_fall = -1;
    int x;
    demo->loaded_timing_valid[channel] = 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        if (samples[x] == WAVEFORM_NO_SAMPLE) continue;
        if (samples[x] < minimum) minimum = samples[x];
        if (samples[x] > maximum) maximum = samples[x];
    }
    if (maximum - minimum < 8) return;
    threshold = minimum + (maximum - minimum) / 2;
    for (x = 1; x < SCOPE_PLOT_WIDTH; ++x) {
        if (samples[x - 1] == WAVEFORM_NO_SAMPLE || samples[x] == WAVEFORM_NO_SAMPLE) {
            first_rise = first_fall = -1; continue;
        }
        if (samples[x - 1] >= threshold && samples[x] < threshold) {
            if (first_rise < 0) first_rise = x;
            else { second_rise = x; break; }
        } else if (first_rise >= 0 && first_fall < 0 &&
                   samples[x - 1] < threshold && samples[x] >= threshold)
            first_fall = x;
    }
    if (first_rise < 0 || second_rise <= first_rise ||
        first_fall <= first_rise || first_fall >= second_rise) return;
    demo->loaded_period_us[channel] =
        (second_rise - first_rise) * demo->loaded_wave.time_us_per_div *
        10.0 / SCOPE_PLOT_WIDTH;
    demo->loaded_duty_percent[channel] =
        100.0 * (first_fall - first_rise) / (second_rise - first_rise);
    demo->loaded_timing_valid[channel] = 1;
}

typedef struct {
    double minimum;
    double maximum;
    double mean;
    double rms;
    int count;
} ChannelStats;

static ChannelStats channel_stats(const DemoSignal *demo, int channel)
{
    ChannelStats result = {0};
    const int16_t *samples = channel ? demo->ch2 : demo->ch1;
    double scale = volts_per_div(demo, channel) / PIXELS_PER_DIV;
    double sum = 0.0, squares = 0.0;
    int zero = channel_zero(demo, channel);
    int x;
    result.minimum = 1e9;
    result.maximum = -1e9;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        if (samples[x] == WAVEFORM_NO_SAMPLE) continue;
        double voltage = (zero - samples[x]) * scale;
        ++result.count;
        if (voltage < result.minimum) result.minimum = voltage;
        if (voltage > result.maximum) result.maximum = voltage;
        sum += voltage;
        squares += voltage * voltage;
    }
    if (result.count) {
        result.mean = sum / result.count;
        result.rms = sqrt(squares / result.count);
    }
    return result;
}

static void format_measurement(const DemoSignal *demo, int id,
                               const ChannelStats stats[2], char *value, size_t size)
{
    int channel = id < DEMO_MEAS_CH2_VPP ? 0 :
                  id <= DEMO_MEAS_CH2_MAX ? 1 :
                  id <= DEMO_MEAS_CH1_DUTY ? 0 :
                  id <= DEMO_MEAS_CH2_DUTY ? 1 : -1;
    if (channel >= 0 && !(channel ? demo->screen.ch2_enabled : demo->screen.ch1_enabled)) {
        snprintf(value, size, "OFF");
        return;
    }
    if (channel >= 0 && !stats[channel].count) {snprintf(value, size, "--"); return;}
    if (id <= DEMO_MEAS_CH2_MAX) {
        const ChannelStats *stat = &stats[channel];
        double number;
        switch (id % 5) {
            case 0: number = stat->maximum - stat->minimum; break;
            case 1: number = stat->rms; break;
            case 2: number = stat->mean; break;
            case 3: number = stat->minimum; break;
            default: number = stat->maximum; break;
        }
        snprintf(value, size, "%.2f V", number);
    } else if (id <= DEMO_MEAS_CH2_DUTY) {
        if (demo->waveform_loaded && !demo->loaded_timing_valid[channel])
            snprintf(value, size, "--");
        else if (demo->waveform_loaded &&
                 (id == DEMO_MEAS_CH1_FREQ || id == DEMO_MEAS_CH2_FREQ))
            snprintf(value, size, "%.2f kHz",
                     1000.0 / demo->loaded_period_us[channel]);
        else if (demo->waveform_loaded &&
                 (id == DEMO_MEAS_CH1_PERIOD || id == DEMO_MEAS_CH2_PERIOD))
            snprintf(value, size, "%.1f us", demo->loaded_period_us[channel]);
        else if (demo->waveform_loaded)
            snprintf(value, size, "%.1f %%", demo->loaded_duty_percent[channel]);
        else if (id == DEMO_MEAS_CH1_FREQ || id == DEMO_MEAS_CH2_FREQ)
            snprintf(value, size, "%.2f kHz",
                     1000.0 / generator_period_us[demo->generator_frequency_index]);
        else if (id == DEMO_MEAS_CH1_PERIOD || id == DEMO_MEAS_CH2_PERIOD)
            snprintf(value, size, "%.1f us",
                     generator_period_us[demo->generator_frequency_index]);
        else if (demo->generator_wave[channel] == DEMO_WAVE_SQUARE)
            snprintf(value, size, "50.0 %%");
        else snprintf(value, size, "--");
    }
}

static void format_cursor_measurement(const DemoSignal *demo, int id, int channel,
                                      char *value, size_t size)
{
    if (id == DEMO_MEAS_CURSOR_DT || id == DEMO_MEAS_CURSOR_INV_DT) {
        double us_per_pixel = current_time_us_per_div(demo) * 10.0 / SCOPE_PLOT_WIDTH;
        double delta;
        if (demo->screen.cursor_mode != SCOPE_CURSOR_TIME) {
            snprintf(value, size, "--");
            return;
        }
        delta = abs(demo->screen.cursor_b - demo->screen.cursor_a) * us_per_pixel;
        if (id == DEMO_MEAS_CURSOR_DT) format_cursor_time(value, size, delta, 0);
        else if (delta <= 0.0) snprintf(value, size, "--");
        else if (delta <= 1.0) format_cursor_number(value, size, 1.0 / delta, "MHz", 0);
        else if (delta <= 1000.0) format_cursor_number(value, size, 1000.0 / delta, "kHz", 0);
        else format_cursor_number(value, size, 1000000.0 / delta, "Hz", 0);
    } else if (id == DEMO_MEAS_CURSOR_DV) {
        double scale;
        if (demo->screen.cursor_mode != SCOPE_CURSOR_VOLTAGE) {
            snprintf(value, size, "--");
            return;
        }
        scale = volts_per_div(demo, channel) / PIXELS_PER_DIV;
        format_cursor_number(value, size,
                 abs(demo->screen.cursor_b - demo->screen.cursor_a) * scale, "V", 0);
    } else if (id == DEMO_MEAS_CURSOR_A || id == DEMO_MEAS_CURSOR_B) {
        int position = id == DEMO_MEAS_CURSOR_A ?
                       demo->screen.cursor_a : demo->screen.cursor_b;
        if (demo->screen.cursor_mode == SCOPE_CURSOR_TIME) {
            double us_per_pixel = current_time_us_per_div(demo) * 10.0 / SCOPE_PLOT_WIDTH;
            format_cursor_time(value, size,
                               (position - demo->screen.trigger_marker_x) * us_per_pixel, 1);
        } else if (demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE) {
            double scale = volts_per_div(demo, channel) / PIXELS_PER_DIV;
            format_cursor_number(value, size,
                     (channel_zero(demo, channel) - position) * scale, "V", 1);
        } else snprintf(value, size, "--");
    }
}

static void update_measurements(DemoSignal *demo)
{
    ChannelStats stats[2] = {0};
    int external=demo->external_capture && !demo->waveform_loaded;
    int i, row, x, y;
    if(!external){stats[0]=channel_stats(demo,0);stats[1]=channel_stats(demo,1);}
    demo->screen.measurement_count = demo->measurement_count;
    for (i = 0; i < demo->measurement_count; ++i) {
        int id = demo->measurement_ids[i];
        demo->screen.measurement_labels[i] = measurement_labels[id];
        /* Hardware measurements arrive with the completed capture. UI edits
           never recalculate them from reduced coordinates or simulation. */
        if(external && id<DEMO_MEAS_CURSOR_DT){
            int ch=id<=DEMO_MEAS_CH1_MAX?0:id<=DEMO_MEAS_CH2_MAX?1:id<=DEMO_MEAS_CH1_DUTY?0:1;
            if(!(ch?demo->screen.ch2_enabled:demo->screen.ch1_enabled))
                snprintf(demo->measurement_value_text[i],sizeof(demo->measurement_value_text[i]),"OFF");
            else if(!demo->external_frame_valid)
                snprintf(demo->measurement_value_text[i],sizeof(demo->measurement_value_text[i]),"--");
        }else format_measurement(demo, id, stats, demo->measurement_value_text[i],
                                sizeof(demo->measurement_value_text[i]));
        demo->screen.measurement_values[i] = demo->measurement_value_text[i];
    }
    demo->screen.cursor_measurement_count = demo->screen.cursor_mode == SCOPE_CURSOR_TIME ?
                                            SCOPE_CURSOR_READOUTS :
                                            demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ? 3 : 0;
    demo->screen.cursor_measurement_rows = demo->screen.cursor_mode == SCOPE_CURSOR_TIME ? 1 :
                                          demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ? 2 : 0;
    for (row = 0; row < demo->screen.cursor_measurement_rows; ++row)
        for (i = 0; i < demo->screen.cursor_measurement_count; ++i) {
            int id = i == 0 ? DEMO_MEAS_CURSOR_A :
                     i == 1 ? DEMO_MEAS_CURSOR_B :
                     i == 3 ? DEMO_MEAS_CURSOR_INV_DT :
                     demo->screen.cursor_mode == SCOPE_CURSOR_TIME ? DEMO_MEAS_CURSOR_DT : DEMO_MEAS_CURSOR_DV;
            demo->screen.cursor_measurement_labels[i] =
                id == DEMO_MEAS_CURSOR_INV_DT ? "FREQ" : measurement_labels[id] + 7;
            format_cursor_measurement(demo, id, row, demo->cursor_measurement_value_text[row][i],
                                      sizeof(demo->cursor_measurement_value_text[row][i]));
            demo->screen.cursor_measurement_values[row][i] = demo->cursor_measurement_value_text[row][i];
        }
    if (!demo->screen.measurement_hidden) {
        scope_screen_measurement_bounds(&demo->screen, &x, &y, NULL, NULL);
        demo->screen.measurement_x = x;
        demo->screen.measurement_y = y;
    }
}

static int measurement_slot(const DemoSignal *demo, int id)
{
    int i;
    for (i = 0; i < demo->measurement_count; ++i)
        if (demo->measurement_ids[i] == id) return i;
    return -1;
}

static int menu_item_count(DemoMenu kind)
{
    if (kind == DEMO_MENU_MEASURE) return SCOPE_MEASURE_CATALOG_ITEMS + 1;
    if (kind == DEMO_MENU_MAIN)
        return (int)(sizeof(main_items) / sizeof(main_items[0]));
    if (kind == DEMO_MENU_BROWSE_TYPE) return 2;
    if (kind == DEMO_MENU_CH1 || kind == DEMO_MENU_CH2) return 5;
    if (kind == DEMO_MENU_TRIGGER) return 7;
    if (kind == DEMO_MENU_CURSOR) return 1;
    if (kind == DEMO_MENU_DISPLAY) return 3;
    if (kind == DEMO_MENU_TIME) return 3;
    if (kind == DEMO_MENU_BROWSE) return DEMO_BROWSE_PAGE;
    if (kind == DEMO_MENU_PC) return 2;
    if (kind == DEMO_MENU_PROCESSING) return 6;
    if (kind == DEMO_MENU_DEBUG) return 6;
    if (kind == DEMO_MENU_GENERATOR) return 3;
    if (kind == DEMO_MENU_FONT) return SCOPE_FONT_COUNT;
    if (kind == DEMO_MENU_SETUP) return 3;
    if (kind == DEMO_MENU_FILES) return 6;
    if (kind == DEMO_MENU_LAYOUT) return 7;
    if (kind == DEMO_MENU_PRESETS) return 5;
    if (kind == DEMO_MENU_PRESET) return 2;
    if (kind == DEMO_MENU_RESPONSE) return 4;
    return 0;
}

static int catalog_id(int index)
{
    return channel_measure_ids[index];
}

static int catalog_index(int id)
{
    int i;
    for (i = 0; i < SCOPE_MEASURE_CATALOG_ITEMS; ++i)
        if (catalog_id(i) == id) return i;
    return 0;
}

static int current_menu_value(const DemoSignal *demo)
{
    int item = demo->screen.menu_selected;
    if (demo->menu_kind == DEMO_MENU_CH1 || demo->menu_kind == DEMO_MENU_CH2) {
        int channel = demo->menu_kind == DEMO_MENU_CH2;
        if (item == 0) return channel ? demo->screen.ch2_enabled : demo->screen.ch1_enabled;
        if (item == 1) return demo->coupling[channel];
        if (item == 2) return demo->probe_ten[channel];
        if (item == 3) return demo->scale_index[channel];
        return 0;
    }
    if (demo->menu_kind == DEMO_MENU_TRIGGER) {
        if (item == 0) return demo->trigger_mode_index;
        if (item == 1) return demo->trigger_source_index;
        if (item == 2) return demo->trigger_edge_index;
        if (item == 6) return demo->screen.trigger_y;
        return demo->trigger_holdoff_index;
    }
    if (demo->menu_kind == DEMO_MENU_CURSOR)
        return demo->screen.cursor_mode;
    if (demo->menu_kind == DEMO_MENU_DISPLAY)
        return item ? demo->screen.font_index : demo->screen.grid_enabled;
    if (demo->menu_kind == DEMO_MENU_PC) return demo->pc_baud_index;
    if (demo->menu_kind == DEMO_MENU_SETUP) return demo->startup_preset;
    if (demo->menu_kind == DEMO_MENU_RESPONSE) return demo->response_enabled[demo->response_channel];
    if(demo->menu_kind==DEMO_MENU_LAYOUT)return item==0?!demo->screen.measurement_hidden:item==1?demo->screen.measurement_horizontal:
        item==2?demo->screen.measurement_x:item==3?demo->screen.measurement_y:item==4?demo->menu_position_x:
        item==5?demo->menu_position_y:demo->screen.split_height-70;
    if (demo->menu_kind == DEMO_MENU_TIME)
        return demo->screen.zoom_enabled ? demo->zoom_time_index : demo->time_index;
    if (demo->menu_kind == DEMO_MENU_PROCESSING)
        return item < 2 ? demo->filter_level[item] :
               item == 2 ? demo->intensity_coloring :
               item == 3 ? demo->persistence_index :
               item == 5 ? demo->record_depth_index : demo->fft_enabled;
    if (demo->menu_kind == DEMO_MENU_GENERATOR)
        return item < 2 ? demo->generator_wave[item] :
                          demo->generator_frequency_index;
    if (demo->menu_kind == DEMO_MENU_DEBUG)
        return item == 3 ? demo->adc_simulation : item == 1 ? (demo->hold_time_ms - 200) / 50 :
                          (demo->double_click_ms - 100) / 50;
    return 0;
}

static int menu_value_count(const DemoSignal *demo)
{
    int item = demo->screen.menu_selected;
    if (demo->menu_kind == DEMO_MENU_CH1 || demo->menu_kind == DEMO_MENU_CH2)
        return item == 0 || item == 1 || item == 2 ? 2 : item == 3 ? 6 : 1;
    if (demo->menu_kind == DEMO_MENU_TRIGGER)
        return item == 0 || item == 2 ? 3 : item == 3 ? 4 :
               item == 6 ? SCOPE_PLOT_HEIGHT : 2;
    if (demo->menu_kind == DEMO_MENU_CURSOR) return 3;
    if (demo->menu_kind == DEMO_MENU_TIME) return item == 0 ?
        (demo->screen.zoom_enabled ? demo->time_index + 1 : TIME_SCALE_COUNT) : 1;
    if (demo->menu_kind == DEMO_MENU_PROCESSING)
        return item < 2 ? 4 : item == 3 ? 3 : item == 5 ? CAPTURE_DEPTH_COUNT : 2;
    if (demo->menu_kind == DEMO_MENU_GENERATOR)
        return item < 2 ? DEMO_WAVE_COUNT :
                          (int)(sizeof(generator_period_us) /
                                sizeof(generator_period_us[0]));
    if(demo->menu_kind==DEMO_MENU_LAYOUT){
        int x,y,width,height;
        if(item<2)return 2;
        if(item==6)return 181;
        if(item<4){scope_screen_measurement_bounds(&demo->screen,&x,&y,&width,&height);
            return item==2?SCOPE_WIDTH-width+1:scope_screen_measurement_bottom(&demo->screen,x,width)-height+1;}
        height=SCOPE_MENU_ROW_Y-SCOPE_MENU_Y+demo->screen.menu_count*SCOPE_MENU_ROW_HEIGHT+6;
        return item==4?SCOPE_WIDTH-SCOPE_MENU_WIDTH+1:SCOPE_HEIGHT-height+1;
    }
    if (demo->menu_kind == DEMO_MENU_PC) return PC_BAUD_COUNT;
    if (demo->menu_kind == DEMO_MENU_SETUP) return 6;
    if (demo->menu_kind == DEMO_MENU_DEBUG) return item == 3 ? 2 : item == 1 ? 27 : 29;
    return 2;
}

/* Availability is shared by rendering, touch, the panel and remote settings. */
static int menu_disabled(const DemoSignal *d,DemoMenu kind,int row)
{
    if(kind==DEMO_MENU_PC)return row==1 || !d->pc_available;
    if(kind==DEMO_MENU_TIME)return row==2;
    if(kind==DEMO_MENU_DEBUG)return row>=4;
    if(kind==DEMO_MENU_BROWSE)return !d->browse_count;
    if(kind==DEMO_MENU_SETUP && row==0)return !d->settings_available;
    if(kind==DEMO_MENU_PRESET)return row==0?!d->settings_available:!(d->preset_valid&(1U<<d->preset_selected));
    if(kind==DEMO_MENU_TRIGGER && row==4)return d->waveform_loaded;
    if(kind==DEMO_MENU_CH1 || kind==DEMO_MENU_CH2)return row==4 && d->waveform_loaded;
    if(kind==DEMO_MENU_RESPONSE){
        unsigned count=demo_signal_response_curve((DemoSignal *)d,(unsigned)d->response_channel)->count;
        return (row==1 && count<2) || (row==2 && !count);
    }
    if(kind==DEMO_MENU_PROCESSING)return d->waveform_loaded && (row==3 || row==5);
    if(kind==DEMO_MENU_MEASURE)return row==SCOPE_MEASURE_CATALOG_ITEMS?!d->measurement_count:
        d->measurement_count==SCOPE_MEASURE_SLOTS && measurement_slot(d,catalog_id(row))<0;
    if(kind==DEMO_MENU_FILES)return row>=3 && (!d->waveform_loaded || (row<5 && d->browse_count<2));
    if(kind==DEMO_MENU_LAYOUT)return ((row==2 || row==3) && d->screen.measurement_hidden) ||
        (row==6 && !d->fft_enabled && !d->screen.zoom_enabled);
    return 0;
}

static unsigned option_disabled(const DemoSignal *d,DemoMenu kind,int row)
{
    if(kind==DEMO_MENU_SETUP && row==0)return (31U&~d->preset_valid)<<1;
    return 0;
}

static int next_menu_option(const DemoSignal *d)
{
    int current=current_menu_value(d),count=menu_value_count(d);
    unsigned unavailable=option_disabled(d,d->menu_kind,d->screen.menu_selected);
    for(int step=1;step<=count;++step){int value=(current+step)%count;if(!(unavailable&(1U<<value)))return value;}
    return current;
}

static void set_menu_options(ScopeScreen *screen, int row,
                             const char *const *options, int count, int selected)
{
    screen->menu_options[row] = options;
    screen->menu_option_count[row] = (uint8_t)count;
    screen->menu_option_selected[row] = (uint8_t)selected;
}

static void refresh_menu(DemoSignal *demo)
{
    ScopeScreen *screen = &demo->screen;
    int i;
    screen->menu_open = demo->menu_kind != DEMO_MENU_NONE;
    screen->measurement_menu = demo->menu_kind == DEMO_MENU_MEASURE;
    screen->menu_count = menu_item_count(demo->menu_kind);
    if (demo->menu_kind == DEMO_MENU_MAIN && demo->waveform_loaded)
        --screen->menu_count;
    screen->menu_page = NULL;
    if (screen->menu_open && !screen->measurement_menu) {
        int menu_height = SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
                          screen->menu_count * SCOPE_MENU_ROW_HEIGHT + 6;
        screen->menu_x = clamp(demo->menu_position_x, 0,
                               SCOPE_WIDTH - SCOPE_MENU_WIDTH);
        screen->menu_y = clamp(demo->menu_position_y, 0,
                               SCOPE_HEIGHT - menu_height);
    }
    for (i = 0; i < SCOPE_MENU_ITEMS; ++i) {
        screen->menu_labels[i] = "";
        screen->menu_values[i] = "";
        screen->menu_options[i] = NULL;
        screen->menu_option_count[i] = 0;
        screen->menu_option_selected[i] = 0;
        screen->menu_stepper[i] = 0;
        screen->menu_disabled[i] = 0;
        screen->menu_option_disabled[i] = 0;
    }
    if (demo->menu_kind == DEMO_MENU_MAIN) {
        screen->menu_title = "MAIN MENU";
        for (i = 0; i < screen->menu_count; ++i)
            screen->menu_labels[i] = main_items[i +
                (demo->waveform_loaded && i>=1 ? 1 : 0)].label;
    } else if (demo->menu_kind == DEMO_MENU_BROWSE_TYPE) {
        screen->menu_title = "BROWSE";
        for (i = 0; i < 2; ++i) screen->menu_labels[i] = browse_type_labels[i];
    } else if (demo->menu_kind == DEMO_MENU_BROWSE) {
        int first = demo->browse_selected / DEMO_BROWSE_PAGE * DEMO_BROWSE_PAGE;
        screen->menu_title = demo->browse_filter ? "BROWSE CSV" : "BROWSE BMP";
        screen->menu_count = demo->browse_count - first;
        if (screen->menu_count > DEMO_BROWSE_PAGE)
            screen->menu_count = DEMO_BROWSE_PAGE;
        if (screen->menu_count <= 0) {
            screen->menu_count = 1;
            screen->menu_labels[0] = "NO CAPTURES";
        } else {
            screen->menu_selected = demo->browse_selected - first;
            for (i = 0; i < screen->menu_count; ++i) {
                const char *name = demo->browse_files[first + i];
                uppercase_filename(demo->browse_display_names[i], name);
                screen->menu_labels[i] = demo->browse_display_names[i];
                screen->menu_values[i] = demo->browse_filter ? "WAVE" : "IMAGE";
            }
            snprintf(demo->menu_page_text, sizeof(demo->menu_page_text), "%02d/%02d",
                     demo->browse_selected + 1, demo->browse_count);
            screen->menu_page = demo->menu_page_text;
        }
    } else if (demo->menu_kind == DEMO_MENU_PC) {
        screen->menu_title = "PC CONNECTION";
        for (i = 0; i < 2; ++i) screen->menu_labels[i] = pc_labels[i];
        screen->menu_values[0] = pc_baud_names[demo->pc_baud_index];
        screen->menu_values[1] = !demo->pc_available ? "UART UNAVAILABLE" : demo->pc_active ? "PC ACTIVE" : "USB-UART READY";
        screen->menu_stepper[0] = 1;
    } else if (demo->menu_kind == DEMO_MENU_CH1 || demo->menu_kind == DEMO_MENU_CH2) {
        int channel = demo->menu_kind == DEMO_MENU_CH2;
        screen->menu_title = channel ? "CHANNEL 2" : "CHANNEL 1";
        for (i = 0; i < 5; ++i) screen->menu_labels[i] = channel_labels[i];
        screen->menu_values[0] = (channel ? screen->ch2_enabled : screen->ch1_enabled) ? "ON" : "OFF";
        screen->menu_values[1] = coupling_names[demo->coupling[channel]];
        screen->menu_values[2] = demo->probe_ten[channel] ? "x10" : "x1";
        screen->menu_values[3] = demo->ch_scale_text[channel];
        screen->menu_values[4] = "START";
        set_menu_options(screen, 0, on_off_names, 2,
                         channel ? screen->ch2_enabled : screen->ch1_enabled);
        set_menu_options(screen, 1, coupling_names, 2, demo->coupling[channel]);
        set_menu_options(screen, 2, probe_names, 2, demo->probe_ten[channel]);
        screen->menu_stepper[3] = 1;
    } else if (demo->menu_kind == DEMO_MENU_TRIGGER) {
        screen->menu_title = "TRIGGER SETTINGS";
        for (i = 0; i < 7; ++i) screen->menu_labels[i] = trigger_labels[i];
        screen->menu_values[0] = mode_names[demo->trigger_mode_index];
        screen->menu_values[1] = demo->trigger_source_index ? "CH2" : "CH1";
        screen->menu_values[2] = edge_names[demo->trigger_edge_index];
        screen->menu_values[3] = holdoff_names[demo->trigger_holdoff_index];
        screen->menu_values[4] = "NOW";
        screen->menu_values[5] = "CENTER";
        screen->menu_values[6] = screen->menu_editing ?
                                 demo->trigger_preview_text : demo->trigger_level_text;
        set_menu_options(screen, 0, mode_names, 3, demo->trigger_mode_index);
        set_menu_options(screen, 1, channel_names, 2, demo->trigger_source_index);
        set_menu_options(screen, 2, edge_names, 3, demo->trigger_edge_index);
        set_menu_options(screen, 3, holdoff_names, 4, demo->trigger_holdoff_index);
        screen->menu_stepper[6] = 1;
    } else if (demo->menu_kind == DEMO_MENU_CURSOR) {
        screen->menu_title = "CURSOR SETTINGS";
        screen->menu_labels[0] = cursor_labels[0];
        screen->menu_values[0] = screen->cursor_mode == SCOPE_CURSOR_TIME ? "TIME" :
                                 screen->cursor_mode == SCOPE_CURSOR_VOLTAGE ? "VERTICAL" : "OFF";
        set_menu_options(screen, 0, cursor_mode_names, 3, screen->cursor_mode);
    } else if (demo->menu_kind == DEMO_MENU_MEASURE) {
        screen->menu_title = "MEASUREMENTS";
        screen->menu_selected = demo->measurement_menu_index;
        for (i = 0; i < SCOPE_MEASURE_CATALOG_ITEMS; ++i) {
            int id = catalog_id(i);
            screen->menu_labels[i] = id <= DEMO_MEAS_CH2_DUTY ?
                                     measurement_labels[id] + 4 : measurement_labels[id];
            screen->menu_values[i] = measurement_slot(demo, id) >= 0 ? "REMOVE" : "ADD";
        }
        screen->menu_labels[SCOPE_MEASURE_CATALOG_ITEMS] = "REMOVE ALL";
    } else if (demo->menu_kind == DEMO_MENU_DISPLAY) {
        screen->menu_title = "DISPLAY";
        screen->menu_labels[0] = display_labels[0];
        screen->menu_values[0] = screen->grid_enabled ? "ON" : "OFF";
        set_menu_options(screen, 0, on_off_names, 2, screen->grid_enabled);
        screen->menu_labels[1] = display_labels[1];
        screen->menu_values[1] = font_names[screen->font_index];
        set_menu_options(screen, 1, font_names, SCOPE_FONT_COUNT, screen->font_index);
        screen->menu_labels[2]="LAYOUT";screen->menu_values[2]="OPEN";
    } else if (demo->menu_kind == DEMO_MENU_TIME) {
        screen->menu_title = "TIME SETTINGS";
        for (i = 0; i < 3; ++i) screen->menu_labels[i] = time_labels[i];
        screen->menu_values[2] = demo->acquisition_rate_text;
        screen->menu_values[0] = demo->time_text;
        screen->menu_values[1] = "CENTER";
        screen->menu_stepper[0] = 1;
    } else if (demo->menu_kind == DEMO_MENU_PROCESSING) {
        screen->menu_title = "PROCESSING";
        for (i = 0; i < 6; ++i) screen->menu_labels[i] = processing_labels[i];
        screen->menu_values[0] = filter_names[demo->filter_level[0]];
        screen->menu_values[1] = filter_names[demo->filter_level[1]];
        screen->menu_values[2] = demo->intensity_coloring ? "ON" : "OFF";
        screen->menu_values[3] = persistence_names[demo->persistence_index];
        screen->menu_values[4] = demo->fft_enabled ? "ON" : "OFF";
        set_menu_options(screen, 0, filter_names, 4, demo->filter_level[0]);
        set_menu_options(screen, 1, filter_names, 4, demo->filter_level[1]);
        set_menu_options(screen, 2, on_off_names, 2, demo->intensity_coloring);
        set_menu_options(screen, 3, persistence_names, 3, demo->persistence_index);
        set_menu_options(screen, 4, on_off_names, 2, demo->fft_enabled);
        screen->menu_values[5] = record_names[demo->record_depth_index];
        set_menu_options(screen, 5, record_names, CAPTURE_DEPTH_COUNT, demo->record_depth_index);
    } else if (demo->menu_kind == DEMO_MENU_DEBUG) {
        screen->menu_title = "DEBUG";
        screen->menu_labels[0] = debug_labels[0];
        screen->menu_values[0] = "OPEN";
        for (i = 1; i < 3; ++i) {
            snprintf(demo->input_timing_text[i - 1], sizeof(demo->input_timing_text[0]),
                     "%d ms", i == 1 ? demo->hold_time_ms : demo->double_click_ms);
            screen->menu_labels[i] = debug_labels[i];
            screen->menu_values[i] = demo->input_timing_text[i - 1];
            screen->menu_stepper[i] = 1;
        }
        screen->menu_labels[3] = debug_labels[3];
        screen->menu_values[3] = on_off_names[demo->adc_simulation];
        set_menu_options(screen, 3, on_off_names, 2, demo->adc_simulation);
        for (i = 4; i < 6; ++i) {
            screen->menu_labels[i] = debug_labels[i];
            screen->menu_values[i] = demo->adc_level_text[i - 4];
        }
    } else if (demo->menu_kind == DEMO_MENU_GENERATOR) {
        screen->menu_title = "DEMO GENERATOR";
        for (i = 0; i < 3; ++i) {
            screen->menu_labels[i] = generator_labels[i];
            screen->menu_stepper[i] = 1;
        }
        screen->menu_values[0] = generator_wave_names[demo->generator_wave[0]];
        screen->menu_values[1] = generator_wave_names[demo->generator_wave[1]];
        for (i = 0; i < 2; ++i) {
            screen->menu_stepper[i] = 0;
            set_menu_options(screen, i, generator_wave_options, DEMO_WAVE_COUNT, demo->generator_wave[i]);
        }
        screen->menu_values[2] = generator_frequency_names[demo->generator_frequency_index];
    } else if (demo->menu_kind == DEMO_MENU_SETUP) {
        screen->menu_title="SETUP";screen->menu_labels[0]="STARTUP";
        screen->menu_values[0]=demo->startup_preset?preset_names[demo->startup_preset-1]:"LAST STATE";
        set_menu_options(screen,0,startup_names,6,demo->startup_preset);
        screen->menu_labels[1]="PRESETS";screen->menu_values[1]="OPEN";
        screen->menu_labels[2]="PC CONNECTION";screen->menu_values[2]="OPEN";

    } else if (demo->menu_kind == DEMO_MENU_PRESETS) {
        screen->menu_title="PRESETS";
        for(i=0;i<5;++i){screen->menu_labels[i]=preset_names[i];screen->menu_values[i]=(demo->preset_valid&(1U<<i))?"SAVED":"EMPTY";}
    } else if (demo->menu_kind == DEMO_MENU_PRESET) {
        screen->menu_title=preset_names[demo->preset_selected];
        screen->menu_labels[0]="SAVE CURRENT SETTINGS";screen->menu_values[0]="SAVE";
        screen->menu_labels[1]="LOAD SETTINGS";screen->menu_values[1]=(demo->preset_valid&(1U<<demo->preset_selected))?"LOAD":"EMPTY";
    } else if (demo->menu_kind == DEMO_MENU_RESPONSE) {
        screen->menu_title=demo->response_channel?"CH2 RESPONSE":"CH1 RESPONSE";
        screen->menu_labels[0]="CALIBRATION";screen->menu_values[0]=demo->response_active?"STOP":"START";
        screen->menu_labels[1]="AMPLITUDE CORRECTION";screen->menu_values[1]=on_off_names[demo->response_enabled[demo->response_channel]];
        set_menu_options(screen,1,on_off_names,2,demo->response_enabled[demo->response_channel]);
        screen->menu_labels[2]="CLEAR POINTS";screen->menu_values[2]="CLEAR";
        screen->menu_labels[3]="EXIT CALIBRATION";screen->menu_values[3]="BACK";
    } else if(demo->menu_kind==DEMO_MENU_FILES){
        static const char *labels[]={"SCREENSHOT","SAVE WAVE","SCREEN + WAVE","PREVIOUS FILE","NEXT FILE","CLOSE FILE"};
        screen->menu_title="FILES";
        for(i=0;i<6;++i){screen->menu_labels[i]=labels[i];screen->menu_values[i]=i<3?"SAVE":i==5?"CLOSE":"OPEN";}
    } else if(demo->menu_kind==DEMO_MENU_LAYOUT){
        static const char *labels[]={"MEASUREMENTS","MEASURE LAYOUT","MEASURE X","MEASURE Y","MENU X","MENU Y","SPLIT HEIGHT"};
        static const char *layouts[]={"COLUMN","ROW"};
        screen->menu_title="LAYOUT";
        for(i=0;i<7;++i)screen->menu_labels[i]=labels[i];
        set_menu_options(screen,0,on_off_names,2,!screen->measurement_hidden);
        set_menu_options(screen,1,layouts,2,screen->measurement_horizontal);
        for(i=2;i<7;++i){
            int value=i==2?screen->measurement_x:i==3?screen->measurement_y:i==4?demo->menu_position_x:
                i==5?demo->menu_position_y:screen->split_height;
            snprintf(demo->layout_value_text[i-2],sizeof(demo->layout_value_text[0]),"%d",screen->menu_editing && screen->menu_selected==i?demo->edit_value+(i==6?70:0):value);
            screen->menu_values[i]=demo->layout_value_text[i-2];screen->menu_stepper[i]=1;
        }
    } else if (demo->menu_kind == DEMO_MENU_FONT) {
        screen->menu_title = "DEBUG / FONT";
        for (i = 0; i < SCOPE_FONT_COUNT; ++i) {
            screen->menu_labels[i] = font_names[i];
            screen->menu_values[i] = i == screen->font_index ? "ACTIVE" : "USE";
        }
    }
    for(i=0;i<screen->menu_count;++i){
        screen->menu_disabled[i]=(uint8_t)menu_disabled(demo,demo->menu_kind,i);
        screen->menu_option_disabled[i]=(uint8_t)option_disabled(demo,demo->menu_kind,i);
    }
    if (screen->menu_editing) {
        i = screen->menu_selected;
        if (demo->menu_kind == DEMO_MENU_CH1 || demo->menu_kind == DEMO_MENU_CH2)
            if (i == 3) {
                int channel = demo->menu_kind == DEMO_MENU_CH2;
                format_scale(demo->edit_scale_text, sizeof(demo->edit_scale_text),
                             demo->edit_value, demo->probe_ten[channel]);
                screen->menu_values[i] = demo->edit_scale_text;
            } else if (i == 2) screen->menu_values[i] = record_names[demo->edit_value]; else {
                screen->menu_values[i] = i == 0 ? demo->edit_value ? "ON" : "OFF" :
                                         i == 1 ? coupling_names[demo->edit_value] :
                                         i == 2 ? demo->edit_value ? "x10" : "x1" : "CONFIRM";
            }
        else if (demo->menu_kind == DEMO_MENU_TRIGGER)
            screen->menu_values[i] = i == 0 ? mode_names[demo->edit_value] :
                                     i == 1 ? demo->edit_value ? "CH2" : "CH1" :
                                     i == 2 ? edge_names[demo->edit_value] :
                                     i == 6 ? demo->trigger_preview_text :
                                              holdoff_names[demo->edit_value];
        else if (demo->menu_kind == DEMO_MENU_CURSOR)
            screen->menu_values[i] = i == 0 ?
                (demo->edit_value == SCOPE_CURSOR_TIME ? "TIME" :
                 demo->edit_value == SCOPE_CURSOR_VOLTAGE ? "VERTICAL" : "OFF") :
                demo->edit_value ? "CH2" : "CH1";
        else if (demo->menu_kind == DEMO_MENU_DISPLAY)
            screen->menu_values[i] = demo->edit_value ? "ON" : "OFF";
        else if (demo->menu_kind == DEMO_MENU_TIME) {
            if (i == 0) {
                format_time_scale(demo->edit_scale_text, sizeof(demo->edit_scale_text),
                                  demo->edit_value);
                screen->menu_values[i] = demo->edit_scale_text;
            }
        }
        else if (demo->menu_kind == DEMO_MENU_PROCESSING)
            screen->menu_values[i] = i < 2 ? filter_names[demo->edit_value] :
                                     i == 3 ? persistence_names[demo->edit_value] :
                                     i == 5 ? record_names[demo->edit_value] :
                                              demo->edit_value ? "ON" : "OFF";
        else if (demo->menu_kind == DEMO_MENU_GENERATOR)
            screen->menu_values[i] = i < 2 ? generator_wave_names[demo->edit_value] :
                                              generator_frequency_names[demo->edit_value];
        else if (demo->menu_kind == DEMO_MENU_PC)
            screen->menu_values[i] = pc_baud_names[demo->edit_value];
        else if (demo->menu_kind == DEMO_MENU_DEBUG && i > 0 && i < 3) {
            snprintf(demo->input_timing_text[i - 1], sizeof(demo->input_timing_text[0]),
                     "%d ms", (i == 1 ? 200 : 100) + demo->edit_value * 50);
        }
        else if (demo->menu_kind == DEMO_MENU_DEBUG && i == 3)
            screen->menu_values[i] = on_off_names[demo->edit_value];
    }
}

static void update_text(DemoSignal *demo, int regenerate)
{
    demo->screen.response_curve=demo_signal_response_curve(demo,(unsigned)demo->response_channel);
    demo->screen.response_channel=demo->response_channel;
    demo->screen.response_active=demo->response_active;
    demo->screen.response_enabled=demo->response_enabled[demo->response_channel];
    int channel;
    demo->screen.fft_cursor_visible = demo->fft_enabled &&
                                      demo->screen.cursor_mode != SCOPE_CURSOR_OFF;
    if (!demo->screen.fft_cursor_visible && demo->screen.cursor_selected == SCOPE_CURSOR_SELECT_FFT)
        demo->screen.cursor_selected = SCOPE_CURSOR_SELECT_A;
    update_trigger_positions(demo);
    for (channel = 0; channel < 2; ++channel) {
        format_scale(demo->ch_scale_text[channel], sizeof(demo->ch_scale_text[channel]),
                     demo->scale_index[channel], demo->probe_ten[channel]);
        snprintf(demo->ch_input_text[channel], sizeof(demo->ch_input_text[channel]),
                 "%s   x%d", coupling_names[demo->coupling[channel]],
                 demo->probe_ten[channel] ? 10 : 1);
        demo->screen.channel_zero_y[channel] = channel_zero(demo, channel);
    }
    if (demo->screen.zoom_enabled || demo->waveform_loaded)
        format_time_value(demo->time_text, sizeof(demo->time_text),
                          current_time_us_per_div(demo));
    else format_time_scale(demo->time_text, sizeof(demo->time_text), demo->time_index);
    {
        int roll = time_us[demo->time_index] > 100000 && demo->trigger_mode_index == 0;
        demo->screen.rolling = roll && !demo->waveform_loaded;
        double rate = demo->external_capture && demo->acquisition_rate_hz > 0 ? demo->acquisition_rate_hz :
            osc_capture_rate(capture_rate_index(time_us[demo->time_index] * 1e-5,
                capture_record_samples(demo->record_depth_index), roll));
        snprintf(demo->acquisition_rate_text, sizeof(demo->acquisition_rate_text),
            rate >= 1e6 ? "%.3g MSa/s AUTO" : rate >= 1e3 ? "%.3g kSa/s AUTO" : "%.3g Sa/s AUTO",
            rate >= 1e6 ? rate / 1e6 : rate >= 1e3 ? rate / 1e3 : rate);
    }
    snprintf(demo->zoom_text, sizeof(demo->zoom_text), "ZOOM x%.7g", zoom_ratio(demo));
    snprintf(demo->horizontal_position_text, sizeof(demo->horizontal_position_text),
             "M POS %+d", demo->time_position);
    snprintf(demo->trigger_source_text, sizeof(demo->trigger_source_text), "CH%d %s",
             demo->trigger_source_index + 1,
             demo->trigger_edge_index == 2 ? "BOTH" :
             demo->trigger_edge_index == 1 ? "FALL" : "RISE");
    snprintf(demo->trigger_level_text, sizeof(demo->trigger_level_text), "%.2f V",
             demo->trigger_voltage);
    snprintf(demo->trigger_preview_text, sizeof(demo->trigger_preview_text), "%.2f V",
             demo->trigger_preview_voltage);
    demo->screen.ch1_scale = demo->ch_scale_text[0];
    demo->screen.ch2_scale = demo->ch_scale_text[1];
    demo->screen.ch1_input = demo->ch_input_text[0];
    demo->screen.ch2_input = demo->ch_input_text[1];
    demo->screen.time_scale = demo->time_text;
    demo->screen.zoom_label = demo->zoom_text;
    demo->screen.horizontal_position = demo->horizontal_position_text;
    demo->screen.ch_position_mode[0] = (uint8_t)demo->position_mode[0];
    demo->screen.ch_position_mode[1] = (uint8_t)demo->position_mode[1];
    demo->screen.time_position_mode = (uint8_t)demo->position_mode[2];
    demo->screen.trigger_mode = mode_names[demo->trigger_mode_index];
    demo->screen.trigger_source = demo->trigger_source_text;
    demo->screen.trigger_source_channel = (uint8_t)demo->trigger_source_index;
    demo->screen.trigger_edge_falling = (uint8_t)(demo->trigger_edge_index == 1);
    demo->screen.trigger_edge_both = (uint8_t)(demo->trigger_edge_index == 2);
    demo->screen.capture_ch1_samples = demo->saved_ch1;
    demo->screen.capture_ch2_samples = demo->saved_ch2;
    demo->screen.history_ch1_samples = demo->history_ch1[0];
    demo->screen.history_ch2_samples = demo->history_ch2[0];
    demo->screen.history_ch1_min_samples = demo->history_minimum[0][0];
    demo->screen.history_ch1_max_samples = demo->history_maximum[0][0];
    demo->screen.history_ch2_min_samples = demo->history_minimum[1][0];
    demo->screen.history_ch2_max_samples = demo->history_maximum[1][0];
    demo->screen.intensity_coloring = (uint8_t)demo->intensity_coloring;
    demo->screen.history_count = demo->waveform_loaded ? 0 : demo->history_count <
        persistence_depth[demo->persistence_index] ? demo->history_count :
        persistence_depth[demo->persistence_index];
    demo->screen.trigger_level = demo->trigger_level_text;
    demo->screen.trigger_preview_level = demo->trigger_preview_text;
    demo->screen.fft_enabled = (uint8_t)demo->fft_enabled;
    demo->screen.fft_ch1_bins = demo->fft_ch1;
    demo->screen.fft_ch2_bins = demo->fft_ch2;
    demo->screen.split_height = demo->fft_enabled ?
                                demo->fft_split_height : demo->zoom_split_height;
    {
        double span = 50000.0 * SCOPE_PLOT_WIDTH / current_time_us_per_div(demo);
        demo_signal_set_fft_range(demo, span, SCOPE_PLOT_WIDTH);
        int periodic_enabled =
            (demo->screen.ch1_enabled && demo->generator_wave[0] != DEMO_WAVE_NOISE) ||
            (demo->screen.ch2_enabled && demo->generator_wave[1] != DEMO_WAVE_NOISE);
        if (!demo->waveform_loaded && periodic_enabled &&
            1000000.0 / generator_period_us[demo->generator_frequency_index] >= span) {
            size_t used = strlen(demo->fft_info_text);
            snprintf(demo->fft_info_text + used, sizeof(demo->fft_info_text) - used, " / ALIAS");
        }
    }
    refresh_menu(demo);
    if (regenerate && (!demo->external_capture || demo->waveform_loaded)) {
        if (demo->waveform_loaded) apply_loaded_view(demo);
        else if (demo->screen.zoom_enabled) apply_zoom_view(demo);
        else if (!demo->screen.running) {
            /* STOP changes the view of the existing record, never its trigger alignment. */
            double step = time_us[demo->time_index] * 10.0 / SCOPE_PLOT_WIDTH;
            int marker = clamp(SCOPE_PLOT_WIDTH / 2 - demo->time_position, 0, SCOPE_PLOT_WIDTH - 1);
            double start = demo->capture_start_us +
                (demo->capture_trigger_x - marker) * demo->capture_step_us +
                marker * (demo->capture_step_us - step);
            render_generator_view(demo, start, step);
            demo->screen.trigger_marker_x = marker;
            demo->history_count = demo->screen.history_count = 0;
        }
        else if (demo->screen.running && demo->trigger_mode_index != 0 &&
                 !trigger_crossing_available(demo)) {
            demo->screen.waiting_for_trigger = 1;
            demo->screen.trigger_locked = 0;
        } else {
            demo->screen.waiting_for_trigger = 0;
            generate(demo, 0);
            demo->history_count = 0;
            record_history(demo);
        }
    }
    if (regenerate && (!demo->external_capture || demo->waveform_loaded)) update_spectrum(demo);
    update_measurements(demo);
}

void demo_signal_refresh(DemoSignal *demo)
{
    update_text(demo, 1);
}

void demo_signal_response_observe(DemoSignal *demo,uint64_t sequence,double frequency,double amplitude)
{
    if(!demo->response_active || sequence==demo->response_seen_sequence)return;
    demo->response_seen_sequence=sequence;
    if(channel_response_add(demo_signal_response_curve(demo,(unsigned)demo->response_channel),frequency,amplitude))update_text(demo,0);
}

ChannelResponse *demo_signal_response_curve(DemoSignal *demo,unsigned channel)
{
    return channel<2?&demo->response[channel+(demo->adc_simulation?2:0)]:NULL;
}

int demo_signal_response_enable(DemoSignal *demo,unsigned channel,int enabled)
{
    ChannelResponse *curve=demo_signal_response_curve(demo,channel);
    if(!curve || (enabled && curve->count<2))return 0;
    demo->response_enabled[channel]=!!enabled;return 1;
}

static void open_menu(DemoSignal *demo, DemoMenu kind, DemoMenu parent)
{
    int height;
    demo->menu_kind = kind;
    demo->menu_parent = parent;
    demo->screen.menu_selected = 0;
    demo->screen.menu_editing = 0;
    demo->screen.measurement_clear_confirm = 0;
    demo->screen.browser_delete_confirm = 0;
    if (kind == DEMO_MENU_MEASURE) {
        demo->measurement_menu_index = catalog_index(demo->measurement_selected);
        demo->measurement_selected = catalog_id(demo->measurement_menu_index);
    }
    if (kind == DEMO_MENU_FONT)
        demo->screen.menu_selected = demo->screen.font_index;
    update_text(demo, 0);
    height = SCOPE_MENU_ROW_Y - SCOPE_MENU_Y + demo->screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6;
    demo->menu_position_x = (SCOPE_WIDTH - SCOPE_MENU_WIDTH) / 2;
    demo->menu_position_y = (SCOPE_HEIGHT - height) / 2;
    refresh_menu(demo);
}

static void close_menu(DemoSignal *demo)
{
    demo->menu_kind = DEMO_MENU_NONE;
    demo->menu_parent = DEMO_MENU_NONE;
    demo->screen.menu_editing = 0;
    demo->screen.measurement_clear_confirm = 0;
    demo->screen.browser_delete_confirm = 0;
    update_text(demo, 0);
}

void demo_signal_set_zoom_center(DemoSignal *demo, int source_x)
{
    double half_window;
    if (!demo->screen.zoom_enabled) return;
    if (demo->waveform_loaded) {
        int limit = loaded_pan_limit(demo);
        demo->zoom_offset = clamp((int)lround((source_x - SCOPE_PLOT_WIDTH / 2) /
                                              loaded_view_step(demo)),
                                  -limit, limit);
        update_text(demo, 1);
        return;
    }
    half_window = SCOPE_PLOT_WIDTH / (2 * zoom_ratio(demo));
    demo->zoom_offset = clamp_position(source_x - SCOPE_PLOT_WIDTH / 2.0,
                                       SCOPE_PLOT_WIDTH / 2.0 - half_window);
    update_text(demo, 1);
}

void demo_signal_move_channel(DemoSignal *demo, int channel, int delta)
{
    if (channel < 0 || channel > 1 || delta == 0) return;
    demo->channel_position[channel] = clamp(demo->channel_position[channel] + delta,
                                             -180, 180);
    demo_signal_notify(demo, channel ? "CH2 VERTICAL POSITION" : "CH1 VERTICAL POSITION");
    update_text(demo, 1);
}

void demo_signal_zoom_channel(DemoSignal *demo, int channel, int steps)
{
    int next;
    if (channel < 0 || channel > 1 || steps == 0) return;
    next = clamp(demo->scale_index[channel] + steps, 0, CONTROL_SCALE_COUNT - 1);
    if (demo->request_scale && !demo->waveform_loaded) {
        if (!demo->request_scale(demo->scale_context, (unsigned)channel, demo_signal_scale_mv(next)))
            demo_signal_notify(demo, "STM32 SCALE UNAVAILABLE");
        return;
    }
    demo->scale_index[channel] = next;
    demo_signal_notify(demo, channel ? "CH2 VOLTS / DIV" : "CH1 VOLTS / DIV");
    update_text(demo, 1);
}

void demo_signal_bind_scale_control(DemoSignal *demo,
    int (*request)(void *, unsigned, int), void *context)
{
    demo->request_scale = request;
    demo->scale_context = context;
}

void demo_signal_accept_scale(DemoSignal *demo, unsigned channel, int millivolts)
{
    int index = control_scale_index(millivolts);
    if (channel >= 2 || index < 0 || demo->waveform_loaded) return;
    if (demo->scale_index[channel] == index) return;
    demo->scale_index[channel] = index;
    update_text(demo, 1);
}

void demo_signal_zoom_time(DemoSignal *demo, int steps)
{
    if (steps == 0) return;
    if (demo->screen.zoom_enabled) {
        demo->zoom_time_index = clamp(demo->zoom_time_index - steps, 0, demo->time_index);
        if (demo->waveform_loaded) {
            int limit = loaded_pan_limit(demo);
            demo->zoom_offset = clamp_position(demo->zoom_offset, limit);
        } else
            demo->zoom_offset = clamp_position(demo->zoom_offset,
                 SCOPE_PLOT_WIDTH / 2.0 - SCOPE_PLOT_WIDTH / (2 * zoom_ratio(demo)));
    } else {
        demo->time_index = clamp(demo->time_index - steps, 0,
                                TIME_SCALE_COUNT - 1);
        if (demo->waveform_loaded) {
            int limit = loaded_pan_limit(demo);
            demo->time_position = clamp(demo->time_position, -limit, limit);
        }
    }
    demo_signal_notify(demo, "TIME SCALE");
    update_text(demo, 1);
}

void demo_signal_set_trigger_preview_y(DemoSignal *demo, int plot_y)
{
    int channel = demo->trigger_source_index;
    plot_y = clamp(plot_y, 8, SCOPE_PLOT_HEIGHT - 9);
    demo->trigger_preview_voltage = (source_zero(demo) - plot_y) *
        volts_per_div(demo, channel) / PIXELS_PER_DIV;
    demo->screen.trigger_preview = 1;
    demo_signal_notify(demo, "TRIGGER LEVEL PREVIEW");
    update_text(demo, 0);
}

static void back_menu(DemoSignal *demo)
{
    if (demo->screen.browser_delete_confirm) {
        demo->screen.browser_delete_confirm = 0;
    } else if (demo->screen.measurement_clear_confirm) {
        demo->screen.measurement_clear_confirm = 0;
    } else if (demo->screen.menu_editing) {
        if (demo->menu_kind == DEMO_MENU_TRIGGER &&
            demo->screen.menu_selected == 6) {
            demo->screen.trigger_preview = 0;
            demo->trigger_preview_voltage = demo->trigger_voltage;
        }
        demo->screen.menu_editing = 0;
        demo_signal_notify(demo, "EDIT CANCELLED");
    } else if (demo->menu_parent == DEMO_MENU_BROWSE_TYPE) {
        open_menu(demo, DEMO_MENU_BROWSE_TYPE, DEMO_MENU_MAIN);
    } else if (demo->menu_parent == DEMO_MENU_DEBUG) {
        open_menu(demo, DEMO_MENU_DEBUG, DEMO_MENU_MAIN);
    } else if (demo->menu_parent == DEMO_MENU_MAIN) {
        open_menu(demo, DEMO_MENU_MAIN, DEMO_MENU_NONE);
        demo_signal_notify(demo, "MAIN MENU");
    } else if(demo->menu_parent==DEMO_MENU_DISPLAY){
        open_menu(demo,DEMO_MENU_DISPLAY,DEMO_MENU_MAIN);
    } else if(demo->menu_parent==DEMO_MENU_SETUP){
        open_menu(demo,DEMO_MENU_SETUP,DEMO_MENU_MAIN);
    } else if(demo->menu_parent==DEMO_MENU_PRESETS){
        open_menu(demo,DEMO_MENU_PRESETS,DEMO_MENU_SETUP);
    } else if(demo->menu_parent==DEMO_MENU_CH1 || demo->menu_parent==DEMO_MENU_CH2){
        DemoMenu parent=demo->menu_parent;
        demo->response_active=0;demo->screen.response_visible=0;
        open_menu(demo,parent,DEMO_MENU_NONE);
    } else {
        close_menu(demo);
        demo_signal_notify(demo, "MENU CLOSED");
    }
}

void demo_signal_notify(DemoSignal *demo, const char *message)
{
    static const char *events[] = {
        "SCREENSHOT SAVED", "WAVEFORM SAVED", "SCREEN AND WAVE SAVED",
        "SAVE FAILED", "NO DATA CARD", "OPEN FAILED", "FILE DELETED", "DELETE FAILED",
        "TRIGGER FORCED", "SINGLE CAPTURE COMPLETE",
        "UART BAUD APPLIED", "UART BAUD FAILED",
        "MEASUREMENT LIST FULL / REMOVE ONE"
    };
    size_t i;
    for (i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        if (strcmp(message, events[i]) == 0) break;
    if (i == sizeof(events) / sizeof(events[0])) return;
    snprintf(demo->status_text, sizeof(demo->status_text), "%s", message);
    demo->notice_until_ms = demo->now_ms + 3000;
    demo->screen.status_visible = 1;
    demo->screen.status_alpha = 255;
    demo->screen.status_warning = (uint8_t)(
        strcmp(message, "SAVE FAILED") == 0 ||
        strcmp(message, "NO DATA CARD") == 0 ||
        strcmp(message, "OPEN FAILED") == 0 ||
        strcmp(message, "DELETE FAILED") == 0 ||
        strcmp(message, "UART BAUD FAILED") == 0 ||
        strcmp(message, "MEASUREMENT LIST FULL / REMOVE ONE") == 0);
}

void demo_signal_init(DemoSignal *demo)
{
    memset(demo, 0, sizeof(*demo));
    demo->screen.ch1_samples = demo->ch1;
    demo->screen.ch2_samples = demo->ch2;
    demo->screen.status_message = demo->status_text;
    demo->scale_index[0] = 2;
    demo->scale_index[1] = 3;
    demo->time_index = DEFAULT_TIME_INDEX;
    demo->record_depth_index = 2;
    demo->pc_baud_index = 4;
    demo->settings_available = 1;
    demo->hold_time_ms = 300;
    demo->double_click_ms = 300;
    demo->generator_wave[0] = DEMO_WAVE_SQUARE;
    demo->generator_wave[1] = DEMO_WAVE_SQUARE;
    demo->generator_frequency_index = 12;
    demo->adc_simulation = 1;
    strcpy(demo->adc_level_text[0], "-");
    strcpy(demo->adc_level_text[1], "-");
    demo->screen.fft_cursor_x = SCOPE_PLOT_WIDTH / 2;
    demo->screen.fft_cursor_level = 128;
    demo->screen.cursor_a = 240;
    demo->screen.cursor_b = 720;
    demo->screen.trigger_marker_x = SCOPE_PLOT_WIDTH / 2;
    demo->screen.menu_x = SCOPE_MENU_X;
    demo->screen.menu_y = SCOPE_MENU_Y;
    demo->menu_position_x = SCOPE_MENU_X;
    demo->menu_position_y = SCOPE_MENU_Y;
    demo->screen.grid_enabled = 1;
    demo->screen.font_index = SCOPE_FONT_INTER;
    demo->zoom_split_height = 94;
    demo->fft_split_height = 94;
    demo->screen.ch1_enabled = 1;
    demo->screen.ch2_enabled = 1;
    demo->screen.running = 1;
    demo->measurement_ids[0] = DEMO_MEAS_CH1_VPP;
    demo->measurement_ids[1] = DEMO_MEAS_CH2_VPP;
    demo->measurement_ids[2] = DEMO_MEAS_CH1_FREQ;
    demo->measurement_count = 3;
    demo->screen.measurement_x = 0;
    demo->screen.measurement_y = SCOPE_BOTTOM_Y;
    demo->screen.measurement_horizontal = 1;
    demo->trigger_voltage = 0.60;
    demo->trigger_preview_voltage = 0.60;
    snprintf(demo->status_text, sizeof(demo->status_text), "READY");
    update_text(demo, 1);
}

void demo_signal_update_time(DemoSignal *demo, uint64_t now_ms)
{
    if(demo->response_active){
        int next=6+(int)((now_ms-demo->response_started_ms)/1000%13);
        if(next!=demo->response_sim_index){demo->response_sim_index=next;update_text(demo,0);}
        if(!demo->external_capture && demo->adc_simulation){
            double frequency=demo_signal_generator_hz(next),sum=0;
            double gain=channel_response_demo_gain((unsigned)demo->response_channel,frequency);
            for(unsigned i=0;i<512;++i){double x=gain*sin(2*PI*i/64.0);sum+=x*x;}
            demo_signal_response_observe(demo,now_ms/200+1,frequency,sqrt(2*sum/512));
        }
    }
    demo->now_ms = now_ms;
    if (demo->screen.status_visible) {
        if (now_ms >= demo->notice_until_ms) {
            demo->screen.status_visible = 0;
            demo->screen.status_alpha = 0;
        } else {
            uint64_t remaining = demo->notice_until_ms - now_ms;
            demo->screen.status_alpha = remaining < 500 ? (uint8_t)(remaining * 255 / 500) : 255;
        }
    }
}

void demo_signal_advance(DemoSignal *demo)
{
    if (demo->external_capture && !demo->waveform_loaded) return;
    if (!demo->screen.running) return;
    if (demo->holdoff_ticks > 0) {
        --demo->holdoff_ticks;
        return;
    }
    if (demo->trigger_mode_index != 0 && !trigger_crossing_available(demo)) {
        demo->screen.waiting_for_trigger = 1;
        demo->screen.trigger_locked = 0;
        return;
    }
    demo->screen.waiting_for_trigger = 0;
    demo->phase = (demo->phase + 3) % SCOPE_PLOT_WIDTH;
    ++demo->capture_sequence;
    generate(demo, 0);
    if (demo->screen.zoom_enabled) {
        save_capture(demo);
        apply_zoom_view(demo);
    }
    update_spectrum(demo);
    record_history(demo);
    update_measurements(demo);
    demo->holdoff_ticks = demo->trigger_holdoff_index * 2;
    if (demo->trigger_mode_index == 2) {
        demo->screen.running = 0;
        demo_signal_notify(demo, "SINGLE CAPTURE COMPLETE");
    }
}

void demo_signal_move_trigger(DemoSignal *demo, int delta)
{
    double scale = volts_per_div(demo, demo->trigger_source_index);
    double min_voltage = (source_zero(demo) - (SCOPE_PLOT_HEIGHT - 9)) * scale / PIXELS_PER_DIV;
    double max_voltage = (source_zero(demo) - 8) * scale / PIXELS_PER_DIV;
    if (!demo->screen.trigger_preview)
        demo->trigger_preview_voltage = demo->trigger_voltage;
    demo->trigger_preview_voltage -= delta * scale / PIXELS_PER_DIV;
    if (demo->trigger_preview_voltage < min_voltage) demo->trigger_preview_voltage = min_voltage;
    if (demo->trigger_preview_voltage > max_voltage) demo->trigger_preview_voltage = max_voltage;
    demo->screen.trigger_preview = 1;
    demo_signal_notify(demo, "TRIGGER PREVIEW / PRESS TRIG TO APPLY");
    update_text(demo, 0);
}

static int measurements_docked_at_bottom(const DemoSignal *demo);
static void redock_measurements(DemoSignal *demo);

static void apply_option(DemoSignal *demo, DemoMenu kind, int item, int value)
{
    int measurement_docked = 0;
    if (kind == DEMO_MENU_CH1 || kind == DEMO_MENU_CH2) {
        int channel = kind == DEMO_MENU_CH2;
        if (item == 0) {
            if (channel) demo->screen.ch2_enabled = (uint8_t)value;
            else demo->screen.ch1_enabled = (uint8_t)value;
        }
        else if (item == 1) demo->coupling[channel] = value;
        else if (item == 2) demo->probe_ten[channel] = value;
        else if (item == 3) {
            if (demo->request_scale && !demo->waveform_loaded) {
                if (!demo->request_scale(demo->scale_context, (unsigned)channel,
                                         demo_signal_scale_mv(value)))
                    demo_signal_notify(demo, "STM32 SCALE UNAVAILABLE");
            } else demo->scale_index[channel] = value;
        }
    } else if (kind == DEMO_MENU_TRIGGER) {
        if (item == 0) demo->trigger_mode_index = value;
        else if (item == 1) {
            int trigger_y = demo->screen.trigger_y;
            demo->trigger_source_index = value;
            demo->trigger_voltage =
                (source_zero(demo) - trigger_y) *
                volts_per_div(demo, demo->trigger_source_index) / PIXELS_PER_DIV;
            demo->trigger_preview_voltage = demo->trigger_voltage;
            demo->screen.trigger_preview = 0;
        }
        else if (item == 2) demo->trigger_edge_index = value;
        else if (item == 6) {
            demo->trigger_voltage = demo->trigger_preview_voltage;
            demo->screen.trigger_preview = 0;
        }
        else demo->trigger_holdoff_index = value;
    } else if (kind == DEMO_MENU_CURSOR) {
        if (item == 0) {
            measurement_docked = measurements_docked_at_bottom(demo);
            demo->screen.cursor_mode = (uint8_t)value;
            demo->screen.cursor_selected = 0;
            demo->screen.cursor_a = value == SCOPE_CURSOR_VOLTAGE ? 125 : 240;
            demo->screen.cursor_b = value == SCOPE_CURSOR_VOLTAGE ? 305 : 720;
        }
    } else if (kind == DEMO_MENU_DISPLAY) {
        if(item)demo->screen.font_index = (uint8_t)value;
        else demo->screen.grid_enabled = (uint8_t)value;
    } else if(kind==DEMO_MENU_LAYOUT){
        if(item==0){if(demo->screen.measurement_hidden==value)demo_signal_ui_toggle_measurements_visible(demo);}
        else if(item==1){if(demo->screen.measurement_horizontal!=value)demo_signal_ui_toggle_measurement_layout(demo);}
        else if(item<4){
            demo_signal_ui_move_measurements(demo,item==2?value-demo->screen.measurement_x:0,item==3?value-demo->screen.measurement_y:0);
        }else if(item<6){
            demo_signal_ui_move_menu(demo,item==4?value-demo->screen.menu_x:0,item==5?value-demo->screen.menu_y:0);
        }else demo_signal_ui_set_split_height(demo,value+70);
    } else if (kind == DEMO_MENU_TIME) {
        if (demo->screen.zoom_enabled) demo->zoom_time_index = clamp(value, 0, demo->time_index);
        else demo->time_index = value;
        if (demo->waveform_loaded) {
            int limit = loaded_pan_limit(demo);
            if (demo->screen.zoom_enabled) demo->zoom_offset = clamp_position(demo->zoom_offset, limit);
            else demo->time_position = clamp(demo->time_position, -limit, limit);
        } else if (demo->screen.zoom_enabled) {
            demo->zoom_offset = clamp_position(demo->zoom_offset,
                SCOPE_PLOT_WIDTH / 2.0 - SCOPE_PLOT_WIDTH / (2 * zoom_ratio(demo)));
        }
    } else if (kind == DEMO_MENU_PROCESSING) {
        if (item < 2) {
            if (demo->filter_level[item] != value)
                demo->history_count = demo->screen.history_count = 0;
            demo->filter_level[item] = value;
        }
        else if (item == 2) demo->intensity_coloring = value;
        else if (item == 3) demo->persistence_index = value;
        else if (item == 5) demo->record_depth_index = value;
        else {
            demo->fft_enabled = value;
            if (demo->fft_enabled && demo->screen.zoom_enabled)
                demo_signal_ui_toggle_zoom(demo);
        }
    } else if (kind == DEMO_MENU_RESPONSE && item == 1) {
        if(!demo_signal_response_enable(demo,(unsigned)demo->response_channel,value)){
            demo_signal_notify(demo,"CALIBRATE AT LEAST TWO FREQUENCIES");
            update_text(demo,0);return;
        }
    } else if (kind == DEMO_MENU_SETUP && item == 0) {
        demo->startup_preset=value;
    } else if (kind == DEMO_MENU_PC && item == 0) {
        demo->pc_baud_index = value;
    } else if (kind == DEMO_MENU_GENERATOR) {
        if (item < 2) demo->generator_wave[item] = value;
        else demo->generator_frequency_index = value;
    }
    if (kind == DEMO_MENU_DEBUG) {
        if (item == 3) {
            demo->adc_simulation = value;demo->response_active=0;
            for(unsigned channel=0;channel<2;++channel)
                if(demo_signal_response_curve(demo,channel)->count<2)demo->response_enabled[channel]=0;
        }
        if (item == 1) demo->hold_time_ms = 200 + value * 50;
        else if (item == 2) {
            demo->double_click_ms = 100 + value * 50;
            memset(demo->channel_press_valid, 0, sizeof(demo->channel_press_valid));
        }
    }
    demo_signal_notify(demo, "SETTING APPLIED");
    update_text(demo, 1);
    if (measurement_docked) redock_measurements(demo);
}

static void apply_edit(DemoSignal *demo)
{
    demo->screen.menu_editing = 0;
    apply_option(demo,demo->menu_kind,demo->screen.menu_selected,demo->edit_value);
}

int demo_signal_set_option(DemoSignal *demo, DemoMenu kind, int item, int value)
{
    int count=0;
    if(!demo||item<0||item>=menu_item_count(kind)||value<0 || menu_disabled(demo,kind,item))return 0;
    if(value<8 && (option_disabled(demo,kind,item)&(1U<<value)))return 0;
    if((kind==DEMO_MENU_CH1||kind==DEMO_MENU_CH2)&&item<4)count=item==3?6:2;
    else if(kind==DEMO_MENU_TRIGGER&&item<4)count=item==0||item==2?3:item==3?4:2;
    else if(kind==DEMO_MENU_TIME&&item==0)count=demo->screen.zoom_enabled?demo->time_index+1:TIME_SCALE_COUNT;
    else if(kind==DEMO_MENU_PROCESSING&&item<6)count=item<2?4:item==3?3:item==5?CAPTURE_DEPTH_COUNT:2;
    else if(kind==DEMO_MENU_CURSOR&&item==0)count=3;
    else if(kind==DEMO_MENU_DISPLAY&&item<2)count=item?SCOPE_FONT_COUNT:2;
    else if(kind==DEMO_MENU_PC&&item==0)count=PC_BAUD_COUNT;
    else if(kind==DEMO_MENU_DEBUG&&item>0&&item<4)count=item==3?2:item==1?27:29;
    else if(kind==DEMO_MENU_GENERATOR&&item<3)count=item<2?DEMO_WAVE_COUNT:
        (int)(sizeof(generator_period_us)/sizeof(generator_period_us[0]));
    else if(kind==DEMO_MENU_SETUP&&item==0)count=6;
    else if(kind==DEMO_MENU_LAYOUT)count=item<2?2:item==6?181:(item==2 || item==4)?1025:601;
    else if(kind==DEMO_MENU_RESPONSE&&item==1)count=2;
    if(!count||value>=count)return 0;
    apply_option(demo,kind,item,value);return 1;
}

void demo_signal_pc_status(DemoSignal *demo, int available, unsigned baud, int active)
{
    int i;
    demo->pc_available = !!available;
    demo->pc_active = !!active;
    for (i = 0; i < PC_BAUD_COUNT; ++i)
        if (pc_bauds[i] == baud) { demo->pc_baud_index = i; break; }
    refresh_menu(demo);
}

void demo_signal_set_trigger_voltage(DemoSignal *demo, double voltage)
{
    if(!demo||!isfinite(voltage))return;
    demo_signal_set_trigger_preview_y(demo,(int)lround(source_zero(demo)-
        voltage*PIXELS_PER_DIV/volts_per_div(demo,demo->trigger_source_index)));
    demo_signal_ui_apply_trigger(demo);
}

static void toggle_measurement(DemoSignal *demo)
{
    int id = demo->measurement_selected;
    int slot = measurement_slot(demo, id);
    if (slot >= 0) {
        int i;
        for (i = slot; i + 1 < demo->measurement_count; ++i)
            demo->measurement_ids[i] = demo->measurement_ids[i + 1];
        --demo->measurement_count;
        demo_signal_notify(demo, "MEASUREMENT REMOVED");
    } else if (demo->measurement_count < SCOPE_MEASURE_SLOTS) {
        demo->measurement_ids[demo->measurement_count++] = id;
        demo_signal_notify(demo, "MEASUREMENT ADDED");
    } else {
        demo_signal_notify(demo, "MEASUREMENT LIST FULL / REMOVE ONE");
    }
}

void demo_signal_ui_open_menu(DemoSignal *demo, DemoMenu kind)
{
    if (kind == DEMO_MENU_MEASURE) demo->measurement_selected = DEMO_MEAS_CH1_VPP;
    open_menu(demo, kind, DEMO_MENU_NONE);
    if (kind == DEMO_MENU_CH1 && !demo->screen.ch1_enabled)
        demo->screen.menu_selected = 0;
    if (kind == DEMO_MENU_CH2 && !demo->screen.ch2_enabled)
        demo->screen.menu_selected = 0;
    update_text(demo, 0);
}

void demo_signal_ui_menu_back(DemoSignal *demo)
{
    if (demo->screen.menu_open) back_menu(demo);
    else open_menu(demo, DEMO_MENU_MAIN, DEMO_MENU_NONE);
    update_text(demo, 0);
}

void demo_signal_ui_dismiss_menu(DemoSignal *demo)
{
    if (!demo->screen.menu_open) return;
    if (demo->screen.menu_editing && demo->menu_kind == DEMO_MENU_TRIGGER &&
        demo->screen.menu_selected == 6) {
        demo->screen.trigger_preview = 0;
        demo->trigger_preview_voltage = demo->trigger_voltage;
    }
    close_menu(demo);
    demo_signal_notify(demo, "MENU CLOSED");
}

void demo_signal_ui_menu_select(DemoSignal *demo, int index)
{
    if (!demo->screen.menu_open || demo->screen.menu_editing ||
        demo->screen.browser_delete_confirm ||
        demo->screen.measurement_clear_confirm ||
        index < 0 || index >= demo->screen.menu_count) return;
    if (demo->menu_kind == DEMO_MENU_MEASURE) {
        demo->measurement_menu_index = index;
        if (index < SCOPE_MEASURE_CATALOG_ITEMS)
            demo->measurement_selected = catalog_id(index);
    } else if (demo->menu_kind == DEMO_MENU_BROWSE && demo->browse_count) {
        demo->browse_selected = demo->browse_selected / DEMO_BROWSE_PAGE *
                                DEMO_BROWSE_PAGE + index;
    }
    demo->screen.menu_selected = index;
    update_text(demo, 0);
}

void demo_signal_ui_menu_choose(DemoSignal *demo, int index, int option)
{
    if (!demo->screen.menu_open || demo->screen.menu_editing ||
        demo->screen.browser_delete_confirm ||
        demo->screen.measurement_clear_confirm ||
        index < 0 || index >= demo->screen.menu_count ||
        option < 0 || option >= demo->screen.menu_option_count[index] ||
        menu_disabled(demo,demo->menu_kind,index) || (option_disabled(demo,demo->menu_kind,index)&(1U<<option))) return;
    if (demo->screen.menu_option_selected[index] == option) return;
    demo_signal_ui_menu_select(demo, index);
    demo->edit_value = option;
    apply_edit(demo);
}

void demo_signal_ui_menu_adjust(DemoSignal *demo, int steps)
{
    int count, index;
    if (!demo->screen.menu_open || !steps) return;
    if (demo->screen.browser_delete_confirm) {
        if (steps % 2) demo->screen.browser_delete_choice =
                           !demo->screen.browser_delete_choice;
        return;
    }
    if (demo->screen.measurement_clear_confirm) {
        if (steps % 2) demo->screen.measurement_clear_choice =
            !demo->screen.measurement_clear_choice;
        return;
    }
    if (demo->screen.menu_editing) {
        if (demo->menu_kind == DEMO_MENU_TRIGGER &&
            demo->screen.menu_selected == 6) {
            demo->edit_value = clamp(demo->edit_value - steps * 8,
                                     8, SCOPE_PLOT_HEIGHT - 9);
            demo->trigger_preview_voltage =
                (source_zero(demo) - demo->edit_value) *
                volts_per_div(demo, demo->trigger_source_index) / PIXELS_PER_DIV;
            demo->screen.trigger_preview = 1;
            update_text(demo, 0);
            return;
        }
        count = menu_value_count(demo);
        index = clamp(demo->edit_value + steps,0,count-1);
        demo->edit_value = index;
    } else {
        if (demo->menu_kind == DEMO_MENU_BROWSE) {
            if (!demo->browse_count) return;
            demo->browse_selected = (demo->browse_selected + steps) % demo->browse_count;
            if (demo->browse_selected < 0) demo->browse_selected += demo->browse_count;
            update_text(demo, 0);
            return;
        }
        count = demo->screen.menu_count;
        index = (demo->screen.menu_selected + steps) % count;
        if (index < 0) index += count;
        int direction=steps<0?-1:1;
        for(int attempt=0;attempt<count && menu_disabled(demo,demo->menu_kind,index);++attempt)index=(index+direction+count)%count;
        demo_signal_ui_menu_select(demo, index);
    }
    update_text(demo, 0);
}

void demo_signal_force_trigger(DemoSignal *demo)
{
    if (demo->waveform_loaded) return;
    if (demo->external_capture) {
        ++demo->capture_sequence;
        demo_signal_notify(demo, "TRIGGER FORCED");
        return;
    }
    demo->phase = (demo->phase + 48) % SCOPE_PLOT_WIDTH;
    ++demo->capture_sequence;
    generate(demo, 1);
    if (demo->screen.zoom_enabled) {
        save_capture(demo);
        apply_zoom_view(demo);
    }
    update_spectrum(demo);
    demo->history_count = 0;
    record_history(demo);
    demo->screen.waiting_for_trigger = 0;
    if (demo->trigger_mode_index == 2) demo->screen.running = 0;
    demo_signal_notify(demo, "TRIGGER FORCED");
    update_text(demo, 0);
}

static void reset_trigger_level(DemoSignal *demo)
{
    demo->trigger_voltage = (source_zero(demo) - PLOT_MIDDLE) *
                            volts_per_div(demo, demo->trigger_source_index) / PIXELS_PER_DIV;
    demo->trigger_preview_voltage = demo->trigger_voltage;
    demo->screen.trigger_preview = 0;
    demo_signal_notify(demo, "TRIGGER LEVEL CENTERED");
    update_text(demo, 1);
}

static int measurements_docked_at_bottom(const DemoSignal *demo)
{
    int x, y, width, height;
    scope_screen_measurement_bounds(&demo->screen, &x, &y, &width, &height);
    return y + height == scope_screen_measurement_bottom(&demo->screen, x, width);
}

static void redock_measurements(DemoSignal *demo)
{
    demo->screen.measurement_y = SCOPE_HEIGHT;
    update_text(demo, 0);
}

DemoAction demo_signal_ui_menu_activate(DemoSignal *demo)
{
    int item;
    int measurement_docked = 0;
    if (!demo->screen.menu_open) return DEMO_ACTION_NONE;
    item = demo->screen.menu_selected;
    if(!demo->screen.browser_delete_confirm && !demo->screen.measurement_clear_confirm && menu_disabled(demo,demo->menu_kind,item))return DEMO_ACTION_NONE;
    if (demo->screen.browser_delete_confirm) {
        int remove = demo->screen.browser_delete_choice;
        demo->screen.browser_delete_confirm = 0;
        update_text(demo, 0);
        return remove ? DEMO_ACTION_BROWSE_DELETE : DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_MEASURE) {
        measurement_docked = measurements_docked_at_bottom(demo);
        if (demo->screen.measurement_clear_confirm) {
            if (demo->screen.measurement_clear_choice) demo->measurement_count = 0;
            demo->screen.measurement_clear_confirm = 0;
        } else if (item == SCOPE_MEASURE_CATALOG_ITEMS) {
            if (demo->measurement_count) {
                demo->screen.measurement_clear_confirm = 1;
                demo->screen.measurement_clear_choice = 0;
            }
        } else toggle_measurement(demo);
        update_text(demo, 0);
        if (measurement_docked) redock_measurements(demo);
    } else if (demo->screen.menu_editing) {
        apply_edit(demo);
    } else if (demo->menu_kind == DEMO_MENU_MAIN) {
        open_menu(demo, main_items[item + (demo->waveform_loaded && item>=1 ? 1 : 0)].target,
                  DEMO_MENU_MAIN);
    } else if (demo->menu_kind == DEMO_MENU_SETUP && item) {
        open_menu(demo,item==1?DEMO_MENU_PRESETS:DEMO_MENU_PC,DEMO_MENU_SETUP);
    } else if(demo->menu_kind==DEMO_MENU_DISPLAY && item==2){
        open_menu(demo,DEMO_MENU_LAYOUT,DEMO_MENU_DISPLAY);
    } else if(demo->menu_kind==DEMO_MENU_FILES){
        static const DemoAction actions[]={DEMO_ACTION_SCREENSHOT,DEMO_ACTION_WAVEFORM,DEMO_ACTION_SCREEN_AND_WAVE,
            DEMO_ACTION_BROWSE_PREV,DEMO_ACTION_BROWSE_NEXT,DEMO_ACTION_BROWSE_CLOSE};
        close_menu(demo);return actions[item];
    } else if (demo->menu_kind == DEMO_MENU_PRESETS) {
        demo->preset_selected=item;open_menu(demo,DEMO_MENU_PRESET,DEMO_MENU_PRESETS);
    } else if (demo->menu_kind == DEMO_MENU_PRESET) {
        return item==0?DEMO_ACTION_PRESET_SAVE:DEMO_ACTION_PRESET_LOAD;
    } else if (demo->menu_kind == DEMO_MENU_DEBUG) {
        if (item == 0) open_menu(demo, DEMO_MENU_GENERATOR, DEMO_MENU_DEBUG);
        else if (item == 3) {
            demo->edit_value = !demo->adc_simulation;
            apply_edit(demo);
        }
        else if (item < 4) {
            demo->edit_value = current_menu_value(demo);
            demo->screen.menu_editing = 1;
            update_text(demo, 0);
        }

    } else if (demo->menu_kind == DEMO_MENU_FONT) {
        demo->screen.font_index = (uint8_t)item;
        demo_signal_notify(demo, "SETTING APPLIED");
        update_text(demo, 0);
    } else if (demo->menu_kind == DEMO_MENU_BROWSE_TYPE) {
        demo->browse_filter = item;
        demo->browse_selected = 0;
        open_menu(demo, DEMO_MENU_BROWSE, DEMO_MENU_BROWSE_TYPE);
        return DEMO_ACTION_BROWSE_REFRESH;
    } else if (demo->menu_kind == DEMO_MENU_BROWSE) {
        return demo->browse_count ? DEMO_ACTION_BROWSE_OPEN : DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_PC && item == 1) {
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_TRIGGER && item == 4) {
        demo_signal_force_trigger(demo);
    } else if (demo->menu_kind == DEMO_MENU_TRIGGER && item == 5) {
        reset_trigger_level(demo);
    } else if (demo->menu_kind == DEMO_MENU_TIME && item == 2) {
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_TIME && item == 1) {
        if (demo->screen.zoom_enabled) demo->zoom_offset = 0;
        else demo->time_position = 0;
        demo_signal_notify(demo, "HORIZONTAL POSITION RESET");
        update_text(demo, 1);
    } else if ((demo->menu_kind==DEMO_MENU_CH1 || demo->menu_kind==DEMO_MENU_CH2) && item==4) {
        DemoMenu parent=demo->menu_kind;
        if(demo->waveform_loaded){demo_signal_notify(demo,"CLOSE SAVED WAVEFORM FIRST");return DEMO_ACTION_NONE;}
        demo->response_active=0;
        demo->response_channel=parent==DEMO_MENU_CH2;
        demo->screen.response_visible=1;
        open_menu(demo,DEMO_MENU_RESPONSE,parent);
    } else if(demo->menu_kind==DEMO_MENU_RESPONSE && item!=1){
        if(item==0){demo_signal_ui_toggle_run(demo);close_menu(demo);}
        else if(item==2){memset(demo_signal_response_curve(demo,(unsigned)demo->response_channel),0,sizeof(ChannelResponse));demo->response_enabled[demo->response_channel]=0;update_text(demo,0);}
        else{demo->response_active=0;demo->screen.response_visible=0;back_menu(demo);}
    } else {
        int short_list = demo->screen.menu_option_count[item]>0;
        if (short_list) {
            demo->edit_value = next_menu_option(demo);
            apply_edit(demo);
        } else {
            demo->edit_value = current_menu_value(demo);
            demo->screen.menu_editing = 1;
            if (demo->menu_kind == DEMO_MENU_TRIGGER && item == 6) {
                demo->trigger_preview_voltage = demo->trigger_voltage;
                demo->screen.trigger_preview = 1;
            }
            update_text(demo, 0);
        }
    }
    return DEMO_ACTION_NONE;
}

DemoAction demo_signal_ui_menu_tap(DemoSignal *demo, int index, int direction)
{
    int small_choice = 0;
    int count;
    if (!demo->screen.menu_open || demo->screen.measurement_clear_confirm ||
        index < 0 || index >= demo->screen.menu_count || menu_disabled(demo,demo->menu_kind,index))
        return DEMO_ACTION_NONE;
    if (demo->screen.menu_editing) back_menu(demo);
    demo_signal_ui_menu_select(demo, index);
    if(demo->screen.menu_option_count[index]){
        demo->edit_value=next_menu_option(demo);
        apply_edit(demo);return DEMO_ACTION_NONE;
    }

    if(demo->menu_kind==DEMO_MENU_LAYOUT && index>=2){
        demo->edit_value=clamp(current_menu_value(demo)+(direction<0?-1:1),0,menu_value_count(demo)-1);
        apply_edit(demo);return DEMO_ACTION_NONE;
    }
    if (demo->menu_kind == DEMO_MENU_CH1 || demo->menu_kind == DEMO_MENU_CH2) {
        if (index == 3) {
            demo_signal_zoom_channel(demo, demo->menu_kind == DEMO_MENU_CH2,
                                     direction < 0 ? -1 : 1);
            return DEMO_ACTION_NONE;
        }
        small_choice = index == 0 || index == 1 || index == 2;
        if (index == 4) {
            return demo_signal_ui_menu_activate(demo);
        }
    } else if (demo->menu_kind == DEMO_MENU_TIME && index == 2) {
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_TIME && index == 0) {
        int step = direction < 0 ? -1 : 1;
        demo_signal_zoom_time(demo, demo->screen.zoom_enabled ? step : -step);
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_TRIGGER) {
        small_choice = index <= 3;
        if (index == 6) {
            demo_signal_move_trigger(demo, (direction < 0 ? 1 : -1) * 8);
            demo_signal_ui_apply_trigger(demo);
            return DEMO_ACTION_NONE;
        }
    } else if (demo->menu_kind == DEMO_MENU_CURSOR) {
        small_choice = index == 0 || index == 1;
    } else if (demo->menu_kind == DEMO_MENU_DISPLAY) {
        small_choice = index == 0;
    } else if (demo->menu_kind == DEMO_MENU_FONT) {
        demo->screen.font_index = (uint8_t)index;
        update_text(demo, 0);
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_PROCESSING) {
        small_choice = 1;
    } else if (demo->menu_kind == DEMO_MENU_DEBUG && index > 0) {
        if (index >= 4) return DEMO_ACTION_NONE;
        demo->edit_value = clamp(current_menu_value(demo) + (direction < 0 ? -1 : 1),
                                 0, menu_value_count(demo) - 1);
        apply_edit(demo);
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_GENERATOR) {
        demo->edit_value = current_menu_value(demo) + (direction < 0 ? -1 : 1);
        count = menu_value_count(demo);
        demo->edit_value %= count;
        if (demo->edit_value < 0) demo->edit_value += count;
        apply_edit(demo);
        return DEMO_ACTION_NONE;
    } else if (demo->menu_kind == DEMO_MENU_PC) {
        if (index == 0) {
            demo->edit_value = clamp(current_menu_value(demo) + (direction < 0 ? -1 : 1),0,PC_BAUD_COUNT-1);
            apply_edit(demo);
            return DEMO_ACTION_NONE;
        }
    }
    if (small_choice) {
        count = menu_value_count(demo);
        demo->edit_value = (current_menu_value(demo) + 1) % count;
        apply_edit(demo);
        return DEMO_ACTION_NONE;
    }
    return demo_signal_ui_menu_activate(demo);
}

void demo_signal_ui_toggle_run(DemoSignal *demo)
{
    if(demo->screen.response_visible){
        demo->response_active=!demo->response_active;
        demo->response_started_ms=demo->now_ms;demo->response_sim_index=6;
        demo->response_seen_sequence=0;update_text(demo,0);return;
    }
    if (demo->waveform_loaded) return;
    demo->screen.running = !demo->screen.running;
    demo->screen.waiting_for_trigger = 0;
    demo_signal_notify(demo, demo->screen.running ?
                       "ACQUISITION RUNNING" : "ACQUISITION STOPPED");
    update_text(demo, 1);
}

void demo_signal_ui_toggle_zoom(DemoSignal *demo)
{
    if (!demo->screen.zoom_enabled) {
        demo->fft_enabled = 0;
        save_capture(demo);
        demo->zoom_time_index = demo->time_index > 0 ? demo->time_index - 1 : 0;
        demo->zoom_offset = 0;
        demo->screen.zoom_enabled = 1;
        close_menu(demo);
        demo_signal_notify(demo, "ZOOM ON");
    } else {
        demo->screen.zoom_enabled = 0;
        demo->screen.trigger_marker_x = demo->saved_trigger_marker_x;
        demo_signal_notify(demo, "ZOOM OFF");
    }
    update_text(demo, 1);
}

void demo_signal_ui_toggle_fine(DemoSignal *demo)
{
    demo->screen.fine_mode = !demo->screen.fine_mode;
}

void demo_signal_ui_apply_trigger(DemoSignal *demo)
{
    if (!demo->screen.trigger_preview) return;
    demo->trigger_voltage = demo->trigger_preview_voltage;
    demo->screen.trigger_preview = 0;
    demo_signal_notify(demo, "TRIGGER LEVEL APPLIED");
    update_text(demo, 1);
}

void demo_signal_ui_pan_time(DemoSignal *demo, int delta)
{
    if (!delta) return;
    if (demo->screen.zoom_enabled) {
        double limit = demo->waveform_loaded ? loaded_pan_limit(demo) :
                       SCOPE_PLOT_WIDTH / 2.0 - SCOPE_PLOT_WIDTH / (2 * zoom_ratio(demo));
        demo->zoom_offset = clamp_position(demo->zoom_offset -
                                  (demo->waveform_loaded ? delta : delta / zoom_ratio(demo)), limit);
    } else {
        int limit = demo->waveform_loaded ? loaded_pan_limit(demo) : 420;
        demo->time_position = clamp(demo->time_position +
                                    (demo->waveform_loaded ? delta : -delta),
                                    -limit, limit);
    }
    update_text(demo, 1);
}

void demo_signal_ui_move_cursor(DemoSignal *demo, int coordinate)
{
    int limit = demo->screen.cursor_mode == SCOPE_CURSOR_TIME ?
                SCOPE_PLOT_WIDTH - 1 : SCOPE_PLOT_HEIGHT - 1;
    int *cursor = demo->screen.cursor_selected ?
                  &demo->screen.cursor_b : &demo->screen.cursor_a;
    if (demo->screen.cursor_mode == SCOPE_CURSOR_OFF) return;
    *cursor = clamp(coordinate, 0, limit);
    update_text(demo, 0);
}

void demo_signal_ui_select_cursor(DemoSignal *demo, int selected)
{
    demo->screen.cursor_selected = selected ? 1 : 0;
    update_text(demo, 0);
}

void demo_signal_ui_cycle_cursor_mode(DemoSignal *demo)
{
    int measurement_docked = measurements_docked_at_bottom(demo);
    demo->screen.cursor_mode = (demo->screen.cursor_mode + 1) % 3;
    demo->screen.cursor_selected = 0;
    demo->screen.cursor_a = demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ? 125 : 240;
    demo->screen.cursor_b = demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ? 305 : 720;
    demo_signal_notify(demo, demo->screen.cursor_mode == SCOPE_CURSOR_OFF ? "CURSORS OFF" :
                       demo->screen.cursor_mode == SCOPE_CURSOR_TIME ?
                       "TIME CURSORS" : "VERTICAL CURSORS");
    update_text(demo, 0);
    if (measurement_docked) redock_measurements(demo);
}

void demo_signal_ui_cycle_trigger_mode(DemoSignal *demo)
{
    demo->trigger_mode_index = (demo->trigger_mode_index + 1) % 3;
    demo->screen.waiting_for_trigger = 0;
    demo_signal_notify(demo, "TRIGGER MODE CHANGED");
    update_text(demo, 1);
}

void demo_signal_ui_reset_channel_position(DemoSignal *demo, int channel)
{
    if (channel < 0 || channel > 1) return;
    demo->channel_position[channel] = centered_channel_position(channel);
    demo_signal_notify(demo, channel ? "CH2 POSITION CENTERED" : "CH1 POSITION CENTERED");
    update_text(demo, 1);
}

void demo_signal_ui_reset_time_position(DemoSignal *demo)
{
    if (demo->screen.zoom_enabled) demo->zoom_offset = 0;
    else demo->time_position = 0;
    demo_signal_notify(demo, "HORIZONTAL POSITION RESET");
    update_text(demo, 1);
}

void demo_signal_ui_move_measurements(DemoSignal *demo, int dx, int dy)
{
    int x, y;
    if (demo->screen.measurement_hidden) return;
    demo->screen.measurement_x += dx;
    demo->screen.measurement_y += dy;
    scope_screen_measurement_bounds(&demo->screen, &x, &y, NULL, NULL);
    demo->screen.measurement_x = x;
    demo->screen.measurement_y = y;
}

void demo_signal_ui_move_menu(DemoSignal *demo, int dx, int dy)
{
    int height;
    if (!demo->screen.menu_open || demo->screen.measurement_menu) return;
    height = SCOPE_MENU_ROW_Y - SCOPE_MENU_Y +
             demo->screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6;
    demo->menu_position_x = clamp(demo->screen.menu_x + dx, 0,
                                  SCOPE_WIDTH - SCOPE_MENU_WIDTH);
    demo->menu_position_y = clamp(demo->screen.menu_y + dy, 0,
                                  SCOPE_HEIGHT - height);
    update_text(demo, 0);
}

void demo_signal_ui_toggle_measurement_layout(DemoSignal *demo)
{
    int x, y;
    int docked = measurements_docked_at_bottom(demo);
    demo->screen.measurement_horizontal = !demo->screen.measurement_horizontal;
    if (!demo->screen.measurement_hidden) {
        if (docked) demo->screen.measurement_y = SCOPE_HEIGHT;
        scope_screen_measurement_bounds(&demo->screen, &x, &y, NULL, NULL);
        demo->screen.measurement_x = x;
        demo->screen.measurement_y = y;
    }
    demo_signal_notify(demo, demo->screen.measurement_horizontal ?
                       "MEASUREMENTS IN A ROW" : "MEASUREMENTS IN A COLUMN");
}

void demo_signal_ui_toggle_measurements_visible(DemoSignal *demo)
{
    int x, y;
    demo->screen.measurement_hidden = !demo->screen.measurement_hidden;
    if (!demo->screen.measurement_hidden) {
        scope_screen_measurement_bounds(&demo->screen, &x, &y, NULL, NULL);
        demo->screen.measurement_x = x;
        demo->screen.measurement_y = y;
    }
}

void demo_signal_ui_set_split_height(DemoSignal *demo, int height)
{
    height = clamp(height, 70, 250);
    if (demo->fft_enabled) demo->fft_split_height = height;
    else if (demo->screen.zoom_enabled) demo->zoom_split_height = height;
    else return;
    update_text(demo, 0);
}

void demo_signal_ui_set_browse_files(DemoSignal *demo, const char *const *names, int count)
{
    int i;
    if (count < 0) count = 0;
    if (count > DEMO_BROWSE_FILES) count = DEMO_BROWSE_FILES;
    demo->browse_count = count;
    for (i = 0; i < count; ++i)
        snprintf(demo->browse_files[i], DEMO_BROWSE_NAME, "%s", names[i]);
    if (demo->browse_selected >= count) demo->browse_selected = count ? count - 1 : 0;
    update_text(demo, 0);
}

const char *demo_signal_ui_browse_filename(const DemoSignal *demo)
{
    return demo->browse_count ? demo->browse_files[demo->browse_selected] : NULL;
}

void demo_signal_ui_request_browse_delete(DemoSignal *demo, int index)
{
    if (!demo->screen.menu_open || demo->menu_kind != DEMO_MENU_BROWSE ||
        !demo->browse_count || demo->screen.browser_delete_confirm) return;
    demo_signal_ui_menu_select(demo, index);
    demo->screen.browser_delete_confirm = 1;
    demo->screen.browser_delete_choice = 0;
    update_text(demo, 0);
}

int demo_signal_set_battery(DemoSignal *demo, unsigned millivolts, int valid)
{
    unsigned percent=valid?battery_level_percent(millivolts):0;
    int changed=demo->screen.battery_known!=!!valid || demo->screen.battery_percent!=percent;
    demo->screen.battery_known=(uint8_t)!!valid;
    demo->screen.battery_percent=(uint8_t)percent;
    return changed;
}

int demo_signal_set_adc_level(DemoSignal *demo, const uint8_t code[2], int valid)
{
    char text[2][4] = {{0}};
    unsigned ch;
    for (ch = 0; ch < 2; ++ch) {
        if (valid) snprintf(text[ch], sizeof(text[ch]), "%u", (unsigned)code[ch]);
        else strcpy(text[ch], "-");
    }
    if (!strcmp(text[0], demo->adc_level_text[0]) &&
        !strcmp(text[1], demo->adc_level_text[1])) return 0;
    memcpy(demo->adc_level_text, text, sizeof(text));
    if (demo->menu_kind == DEMO_MENU_DEBUG) refresh_menu(demo);
    return 1;
}

void demo_signal_export_wave(const DemoSignal *demo, DemoWaveCapture *wave)
{
    int channel;
    memcpy(wave->ch1, demo->ch1, sizeof(wave->ch1));
    memcpy(wave->ch2, demo->ch2, sizeof(wave->ch2));
    for (channel = 0; channel < 2; ++channel) {
        wave->zero_y[channel] = demo->screen.channel_zero_y[channel];
        wave->mv_per_div[channel] = demo_signal_scale_mv(demo->scale_index[channel]) *
                                    (demo->probe_ten[channel] ? 10 : 1);
        wave->scale_index[channel] = demo->scale_index[channel];
        wave->probe_ten[channel] = demo->probe_ten[channel];
        wave->enabled[channel] = channel ? demo->screen.ch2_enabled :
                                           demo->screen.ch1_enabled;
    }
    wave->time_index = demo->screen.zoom_enabled ? demo->zoom_time_index : demo->time_index;
    wave->time_us_per_div = current_time_us_per_div(demo);
    wave->trigger_marker_x = demo->screen.trigger_marker_x;
    wave->trigger_y = demo->screen.trigger_y;
    wave->trigger_source_channel = demo->trigger_source_index;
    wave->trigger_edge = demo->trigger_edge_index;
}

void demo_signal_ui_show_wave(DemoSignal *demo, const char *name,
                              const DemoWaveCapture *wave)
{
    int channel;
    demo->loaded_wave = *wave;
    if (!isfinite(demo->loaded_wave.time_us_per_div) ||
        demo->loaded_wave.time_us_per_div <= 0.0 ||
        demo->loaded_wave.time_us_per_div > 1000000.0) {
        int legacy = wave->time_index >= 0 && wave->time_index <= 6 ? wave->time_index + 9 : DEFAULT_TIME_INDEX;
        demo->loaded_wave.time_us_per_div = time_us[legacy];
    }
    demo->loaded_wave.time_index = 0;
    for (channel = 1; channel < TIME_SCALE_COUNT; ++channel)
        if (fabs(log(time_us[channel] / demo->loaded_wave.time_us_per_div)) <
            fabs(log(time_us[demo->loaded_wave.time_index] / demo->loaded_wave.time_us_per_div)))
            demo->loaded_wave.time_index = channel;
    for (channel = 0; channel < 2; ++channel) {
        demo->loaded_wave.zero_y[channel] = clamp(wave->zero_y[channel], 0,
                                                  SCOPE_PLOT_HEIGHT - 1);
        demo->loaded_wave.scale_index[channel] = clamp(wave->scale_index[channel], 0, 5);
        demo->loaded_wave.probe_ten[channel] = !!wave->probe_ten[channel];
        if (demo->loaded_wave.mv_per_div[channel] <= 0 ||
            demo->loaded_wave.mv_per_div[channel] > 50000)
            demo->loaded_wave.mv_per_div[channel] =
                demo_signal_scale_mv(demo->loaded_wave.scale_index[channel]) *
                (demo->loaded_wave.probe_ten[channel] ? 10 : 1);
        demo->scale_index[channel] = demo->loaded_wave.scale_index[channel];
        demo->probe_ten[channel] = demo->loaded_wave.probe_ten[channel];
        demo->channel_position[channel] = demo->loaded_wave.zero_y[channel] -
                                          (channel ? 330 : 200);
    }
    demo->time_index = demo->loaded_wave.time_index;
    demo->time_position = 0;
    demo->zoom_offset = 0;
    demo->screen.zoom_enabled = 0;
    demo->fft_enabled = 0;
    demo->intensity_coloring = 0;
    demo->screen.running = 0;
    demo->screen.waiting_for_trigger = 0;
    demo->screen.ch1_enabled = !!wave->enabled[0];
    demo->screen.ch2_enabled = !!wave->enabled[1];
    demo->loaded_wave.trigger_marker_x = clamp(wave->trigger_marker_x, 0,
                                               SCOPE_PLOT_WIDTH - 1);
    demo->trigger_source_index = !!wave->trigger_source_channel;
    demo->trigger_edge_index = clamp(wave->trigger_edge, 0, 2);
    demo->trigger_voltage =
        (channel_zero(demo, demo->trigger_source_index) - wave->trigger_y) *
        volts_per_div(demo, demo->trigger_source_index) / PIXELS_PER_DIV;
    demo->trigger_preview_voltage = demo->trigger_voltage;
    demo->screen.trigger_preview = 0;
    uppercase_filename(demo->browser_title_text, name);
    demo->screen.browser_title = demo->browser_title_text;
    demo->screen.waveform_loaded = 1;
    demo->waveform_loaded = 1;
    for(channel=0;channel<2;++channel){
        demo->screen.density_source[channel].samples=0;
        demo->screen.density_levels[channel]=NULL;
        demo->screen.connections[channel]=NULL;
        demo->screen.peak_min_samples[channel]=demo->screen.peak_max_samples[channel]=NULL;
        demo->screen.peak_strength[channel]=NULL;
    }
    analyze_loaded_timing(demo, 0);
    analyze_loaded_timing(demo, 1);
    close_menu(demo);
    update_text(demo, 1);
}

void demo_signal_ui_show_capture(DemoSignal *demo, const char *name,
                                 const uint32_t *pixels)
{
    uppercase_filename(demo->browser_title_text, name);
    demo->screen.browser_title = demo->browser_title_text;
    demo->screen.browser_pixels = pixels;
    demo->screen.browser_visible = 1;
    close_menu(demo);
}

void demo_signal_ui_close_capture(DemoSignal *demo)
{
    demo->screen.browser_visible = 0;
    demo->screen.waveform_loaded = 0;
    demo->waveform_loaded = 0;
    demo->screen.browser_pixels = NULL;
    open_menu(demo, DEMO_MENU_BROWSE, DEMO_MENU_BROWSE_TYPE);
}

int demo_signal_menu_encoder(const DemoSignal *demo,DemoControl control)
{
    if(!demo->screen.menu_open)return 0;
    if(control==DEMO_ENC_FUNCTION)return 1;
    DemoMenu kind=demo->menu_kind,parent=demo->menu_parent;
    if(control==DEMO_ENC_CH1)return kind==DEMO_MENU_CH1 || parent==DEMO_MENU_CH1;
    if(control==DEMO_ENC_CH2)return kind==DEMO_MENU_CH2 || parent==DEMO_MENU_CH2;
    if(control==DEMO_ENC_TIME)return kind==DEMO_MENU_CURSOR || kind==DEMO_MENU_TIME;
    if(control==DEMO_ENC_TRIGGER)return kind==DEMO_MENU_TRIGGER || parent==DEMO_MENU_TRIGGER;
    return 0;
}

void demo_signal_rotate(DemoSignal *demo, DemoControl control, int steps)
{
    int channel;
    if (steps == 0) return;
    if(demo_signal_menu_encoder(demo,control)){
        demo_signal_ui_menu_adjust(demo,demo->screen.menu_editing?steps:-steps);return;
    }
    if (control == DEMO_ENC_CH1 || control == DEMO_ENC_CH2) {
        channel = control == DEMO_ENC_CH2;
        if (demo->position_mode[channel]) {
            demo_signal_move_channel(demo, channel,
                                     -steps * (demo->screen.fine_mode ? 1 : 8));
            return;
        } else {
            demo_signal_zoom_channel(demo, channel, -steps);
            return;
        }
    } else if (control == DEMO_ENC_TIME) {
        if (demo->screen.zoom_enabled) {
            if (demo->position_mode[2]) {
                double max_offset = demo->waveform_loaded ? loaded_pan_limit(demo) :
                                    SCOPE_PLOT_WIDTH / 2.0 -
                                    SCOPE_PLOT_WIDTH / (2 * zoom_ratio(demo));
                double delta = steps * (demo->screen.fine_mode ? 1 : 16);
                if (!demo->waveform_loaded) delta /= zoom_ratio(demo);
                demo->zoom_offset = clamp_position(demo->zoom_offset + delta, max_offset);
                demo_signal_notify(demo, "ZOOM / SCROLL CAPTURE");
            } else {
                demo_signal_zoom_time(demo, steps);
                return;
            }
        } else if (demo->position_mode[2]) {
            int limit = demo->waveform_loaded ? loaded_pan_limit(demo) : 420;
            demo->time_position = clamp(demo->time_position -
                                        steps * (demo->screen.fine_mode ? 1 : 16),
                                        -limit, limit);
            demo_signal_notify(demo, "HORIZONTAL POSITION");
        } else {
            demo->time_index = clamp(demo->time_index - steps, 0,
                                    TIME_SCALE_COUNT - 1);
            if (demo->waveform_loaded) {
                int limit = loaded_pan_limit(demo);
                demo->time_position = clamp(demo->time_position, -limit, limit);
            }
            demo_signal_notify(demo, "TIME / DIV");
        }
    } else if (control == DEMO_ENC_TRIGGER) {
        demo_signal_move_trigger(demo, -steps * (demo->screen.fine_mode ? 1 : 8));
        return;
    } else if (control == DEMO_ENC_FUNCTION) {
        if (demo->fft_enabled && demo->screen.cursor_selected == SCOPE_CURSOR_SELECT_FFT) {
            int value = demo->screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ?
                        demo->screen.fft_cursor_level : demo->screen.fft_cursor_x;
            demo_signal_ui_move_fft_cursor(demo, value + steps * (demo->screen.fine_mode ? 1 : 8));
            return;
        } else if (demo->screen.cursor_mode != SCOPE_CURSOR_OFF) {
            int *selected = demo->screen.cursor_selected ? &demo->screen.cursor_b : &demo->screen.cursor_a;
            int limit = demo->screen.cursor_mode == SCOPE_CURSOR_TIME ?
                        SCOPE_PLOT_WIDTH - 1 : SCOPE_PLOT_HEIGHT - 1;
            *selected = clamp(*selected +
                              steps * (demo->screen.fine_mode ? 1 : 8), 0, limit);
            demo_signal_notify(demo, "FUNC / MOVE CURSOR");
        }
    }
    update_text(demo, 1);
}

DemoAction demo_signal_press_at(DemoSignal *demo, DemoControl control,
                                int long_press, uint64_t now_ms)
{
    DemoAction action = DEMO_ACTION_NONE;
    int forced_frame = 0;
    if(demo->screen.browser_visible && control==DEMO_ENC_FUNCTION)return long_press?DEMO_ACTION_BROWSE_PREV:DEMO_ACTION_BROWSE_NEXT;
    if(demo->menu_kind==DEMO_MENU_BROWSE && control==DEMO_ENC_FUNCTION && long_press){
        demo_signal_ui_request_browse_delete(demo,demo->screen.menu_selected);return DEMO_ACTION_NONE;
    }
    if(demo_signal_menu_encoder(demo,control)){
        if(long_press){back_menu(demo);update_text(demo,0);return DEMO_ACTION_NONE;}
        return demo_signal_ui_menu_activate(demo);
    }
    if (long_press && demo->screen.menu_open &&
        ((control == DEMO_BTN_CH1 && demo->menu_kind == DEMO_MENU_CH1) ||
         (control == DEMO_BTN_CH2 && demo->menu_kind == DEMO_MENU_CH2) ||
         (control == DEMO_BTN_CURSOR && demo->menu_kind == DEMO_MENU_CURSOR) ||
         (control == DEMO_BTN_MODE && demo->menu_kind == DEMO_MENU_TRIGGER) ||
         control == DEMO_BTN_MENU)) {
        demo_signal_ui_dismiss_menu(demo);
        if (control == DEMO_BTN_CH1 || control == DEMO_BTN_CH2)
            demo->channel_press_valid[control == DEMO_BTN_CH2] = 0;
        update_text(demo, 0);
        return DEMO_ACTION_NONE;
    }
    if (control == DEMO_ENC_CH1 || control == DEMO_ENC_CH2) {
        int channel = control == DEMO_ENC_CH2;
        if (long_press) {
            demo_signal_ui_reset_channel_position(demo, channel);
        } else {
            demo->position_mode[channel] = !demo->position_mode[channel];
            demo_signal_notify(demo, demo->position_mode[channel] ?
                               "VERTICAL POSITION" : "VOLTS / DIV");
        }
    } else if (control == DEMO_ENC_TIME) {
        if (long_press) {
            demo_signal_ui_reset_time_position(demo);
        } else {
            demo->position_mode[2] = !demo->position_mode[2];
            demo_signal_notify(demo, demo->position_mode[2] ?
                               "HORIZONTAL POSITION" : "TIME / DIV");
        }
    } else if (control == DEMO_ENC_TRIGGER) {
        if (long_press) {
            reset_trigger_level(demo);
        } else demo_signal_ui_apply_trigger(demo);
    } else if (control == DEMO_ENC_FUNCTION) {
        if (long_press) {
            demo_signal_ui_toggle_fine(demo);
        } else if(demo->screen.response_visible && !demo->screen.menu_open){
            demo_signal_ui_toggle_run(demo);
        } else if (demo->screen.cursor_mode != SCOPE_CURSOR_OFF) {
            demo->screen.cursor_selected = (demo->screen.cursor_selected + 1) % (demo->fft_enabled ? 3 : 2);
        }
    } else if (control == DEMO_BTN_CH1 || control == DEMO_BTN_CH2) {
        int channel = control == DEMO_BTN_CH2;
        DemoMenu channel_menu = channel ? DEMO_MENU_CH2 : DEMO_MENU_CH1;
        if (long_press) {
            demo->channel_press_valid[channel] = 0;
            open_menu(demo, channel_menu, DEMO_MENU_NONE);
            demo_signal_notify(demo, channel ? "CH2 SETTINGS" : "CH1 SETTINGS");
            update_text(demo, 0);
            return DEMO_ACTION_NONE;
        }
        uint8_t *enabled = channel ? &demo->screen.ch2_enabled : &demo->screen.ch1_enabled;
        int is_second = demo->channel_press_valid[channel] &&
                        now_ms >= demo->last_channel_press_ms[channel] &&
                        now_ms - demo->last_channel_press_ms[channel] <= (uint64_t)demo->double_click_ms;
        if (!*enabled) {
            *enabled = 1;
            demo_signal_notify(demo, channel ? "CH2 ON" : "CH1 ON");
        } else if (is_second) {
            *enabled = 0;
            if (demo->menu_kind == channel_menu) close_menu(demo);
            demo->channel_press_valid[channel] = 0;
            demo_signal_notify(demo, channel ? "CH2 OFF" : "CH1 OFF");
        } else if (demo->menu_kind == channel_menu) {
            close_menu(demo);
            demo_signal_notify(demo, "CHANNEL MENU CLOSED");
        } else {
            open_menu(demo, channel_menu, DEMO_MENU_NONE);
            demo_signal_notify(demo, channel ? "CH2 SETTINGS" : "CH1 SETTINGS");
        }
        if (!is_second || *enabled) {
            demo->last_channel_press_ms[channel] = now_ms;
            demo->channel_press_valid[channel] = 1;
        }
    } else if (control == DEMO_BTN_CURSOR) {
        if (long_press) {
            open_menu(demo, DEMO_MENU_CURSOR, DEMO_MENU_NONE);
        } else {
            demo_signal_ui_cycle_cursor_mode(demo);
        }
    } else if (control == DEMO_BTN_MODE) {
        if (long_press) {
            open_menu(demo, DEMO_MENU_TRIGGER, DEMO_MENU_NONE);
            demo_signal_notify(demo, "TRIGGER SETTINGS");
        } else {
            demo_signal_ui_cycle_trigger_mode(demo);
        }
    } else if (control == DEMO_BTN_MENU) {
        if (demo->screen.browser_visible) {
            demo_signal_ui_close_capture(demo);
        } else if (long_press) {
            demo_signal_ui_toggle_zoom(demo);
        } else if (demo->screen.menu_open) {
            back_menu(demo);
        } else {
            open_menu(demo,demo->screen.response_visible?DEMO_MENU_RESPONSE:DEMO_MENU_MAIN,DEMO_MENU_NONE);
            demo_signal_notify(demo, "MAIN MENU");
        }
    } else if (control == DEMO_BTN_RUN) {
        if (demo->waveform_loaded) {
            if (!long_press) action = DEMO_ACTION_BROWSE_CLOSE;
        } else if (long_press) {
            demo_signal_force_trigger(demo);
            forced_frame = 1;
        } else demo_signal_ui_toggle_run(demo);
    }
    update_text(demo, action == DEMO_ACTION_NONE && !forced_frame);
    return action;
}

DemoAction demo_signal_press(DemoSignal *demo, DemoControl control, int long_press)
{
    demo->synthetic_ms += 1000;
    return demo_signal_press_at(demo, control, long_press, demo->synthetic_ms);
}
