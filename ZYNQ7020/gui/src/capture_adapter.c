#include "capture_adapter.h"
#include "capture_settings.h"
#include "input_range.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
void capture_request_from_demo(const DemoSignal *d, CaptureRequest *r)
{
    DemoWaveCapture wave;
    unsigned ch;
    demo_signal_export_wave(d, &wave);
    memset(r, 0, sizeof(*r));
    r->duration_seconds = wave.time_us_per_div * 10e-6;
    r->overview_duration_seconds = demo_signal_time_scale_us(d->time_index) * 10e-6;
    if (d->screen.zoom_enabled)
        r->zoom_offset_seconds = d->zoom_offset * r->overview_duration_seconds / SCOPE_PLOT_WIDTH;
    r->running = d->screen.running;
    r->record_samples = capture_record_samples(d->record_depth_index);
    r->roll = r->overview_duration_seconds > 1.0 && d->trigger_mode_index == 0;
    r->spectrum_enabled = d->fft_enabled;
    r->debug.enabled = d->adc_simulation;
    r->debug.wave[0] = d->generator_wave[0];
    r->debug.wave[1] = d->generator_wave[1];
    r->debug.offset_fraction[0] = d->coupling[0] ? 0.5 : 0;
    r->debug.offset_fraction[1] = d->coupling[1] ? 0.5 : 0;
    r->trigger_mode = d->trigger_mode_index;
    r->trigger_channel = d->trigger_source_index;
    r->trigger_edge = d->trigger_edge_index;
    r->trigger_voltage = d->trigger_voltage;
    r->force_sequence = d->capture_sequence;
    {
        static const uint64_t holdoff[] = {0,2000,10000,20000};
        r->holdoff_samples = holdoff[d->trigger_holdoff_index];
    }
    r->debug.frequency_hz = demo_signal_generator_hz(d->generator_frequency_index);
    r->pan_fraction = 0.5 + (double)d->time_position / SCOPE_PLOT_WIDTH;
    if (r->pan_fraction < 0)
        r->pan_fraction = 0;
    if (r->pan_fraction > 1)
        r->pan_fraction = 1;
    for (ch = 0; ch < 2; ++ch) {
        r->zero_y[ch] = wave.zero_y[ch];
        r->pixels_per_volt[ch] = (SCOPE_PLOT_HEIGHT / 8.0) * 1000 / wave.mv_per_div[ch];
        r->adc_volts_per_code[ch] = input_range_volts_per_code((unsigned)d->scale_index[ch]) *
                                  (d->probe_ten[ch] ? 10 : 1);
    }
}
void capture_display_apply(DemoSignal *d, const DisplayFrame *f)
{
    int i;
    d->acquisition_rate_hz = f->sample_rate_hz;
    if (d->external_capture) {
        d->screen.trigger_locked = f->trigger_sample != CAPTURE_NO_TRIGGER;
        d->screen.trigger_marker_x = (int)lround(fmax(0, fmin(SCOPE_PLOT_WIDTH - 1, f->trigger_x)));
        if (d->screen.zoom_enabled) {
            d->screen.zoom_window_start = f->overview_start;
            d->screen.zoom_window_end = f->overview_end;
        }
    }
    memcpy(d->ch1, f->y[0], sizeof(d->ch1));
    memcpy(d->ch2, f->y[1], sizeof(d->ch2));
    d->screen.ch1_samples = d->ch1;
    d->screen.ch2_samples = d->ch2;
    memcpy(d->minimum, f->y_min, sizeof(d->minimum));
    memcpy(d->maximum, f->y_max, sizeof(d->maximum));
    memcpy(d->saved_ch1, f->overview[0], sizeof(d->saved_ch1));
    memcpy(d->saved_ch2, f->overview[1], sizeof(d->saved_ch2));
    demo_signal_present_external(d, f->sequence);
    memcpy(d->fft_ch1, f->fft[0], sizeof(d->fft_ch1));
    memcpy(d->fft_ch2, f->fft[1], sizeof(d->fft_ch2));
    demo_signal_set_fft_range(d, f->sample_rate_hz / 2.0);
    d->screen.fft_ch1_bins = d->fft_ch1;
    d->screen.fft_ch2_bins = d->fft_ch2;
    for (i = 0; i < d->measurement_count; ++i) {
        int id = d->measurement_ids[i], ch = id <= DEMO_MEAS_CH1_MAX    ? 0
                                             : id <= DEMO_MEAS_CH2_MAX  ? 1
                                             : id <= DEMO_MEAS_CH1_DUTY ? 0
                                                                        : 1;
        char *text = d->measurement_value_text[i];
        size_t size = sizeof(d->measurement_value_text[i]);
        if (id >= DEMO_MEAS_CURSOR_DT)
            continue;
        if (!(ch ? d->screen.ch2_enabled : d->screen.ch1_enabled)) {
            snprintf(text, size, "OFF");
            continue;
        }
        if (!f->view_samples) {snprintf(text, size, "--"); continue;}
        if (id <= DEMO_MEAS_CH2_MAX) {
            double value;
            switch (id % 5) {
            case 0:
                value = f->maximum[ch] - f->minimum[ch];
                break;
            case 1:
                value = f->rms[ch];
                break;
            case 2:
                value = f->mean[ch];
                break;
            case 3:
                value = f->minimum[ch];
                break;
            default:
                value = f->maximum[ch];
                break;
            }
            if (isfinite(value)) snprintf(text, size, "%.2f V", value);
            else snprintf(text, size, "--");
        } else if (id == DEMO_MEAS_CH1_FREQ || id == DEMO_MEAS_CH2_FREQ)
            snprintf(text, size, "%.2f kHz", f->frequency_hz[ch] / 1000);
        else if (id == DEMO_MEAS_CH1_PERIOD || id == DEMO_MEAS_CH2_PERIOD) {
            if (f->frequency_hz[ch] > 0)
                snprintf(text, size, "%.1f us", 1e6 / f->frequency_hz[ch]);
            else
                snprintf(text, size, "--");
        } else if (id == DEMO_MEAS_CH1_DUTY || id == DEMO_MEAS_CH2_DUTY)
            snprintf(text, size, "%.1f %%", f->duty[ch]);
    }
}
