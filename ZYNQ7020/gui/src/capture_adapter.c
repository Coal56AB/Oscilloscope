#include "capture_adapter.h"
#include <stdio.h>
#include <string.h>
void capture_request_from_demo(const DemoSignal *d, CaptureRequest *r)
{
    DemoWaveCapture wave;
    unsigned ch;
    demo_signal_export_wave(d, &wave);
    r->duration_seconds = wave.time_us_per_div * 10e-6;
    r->running = d->screen.running;
    r->pan_fraction = 0.5 + (double)(d->time_position + d->zoom_offset) / SCOPE_PLOT_WIDTH;
    if (r->pan_fraction < 0)
        r->pan_fraction = 0;
    if (r->pan_fraction > 1)
        r->pan_fraction = 1;
    for (ch = 0; ch < 2; ++ch) {
        r->zero_y[ch] = wave.zero_y[ch];
        r->pixels_per_volt[ch] = (SCOPE_PLOT_HEIGHT / 8.0) * 1000 / wave.mv_per_div[ch];
    }
}
void capture_display_apply(DemoSignal *d, const DisplayFrame *f)
{
    int i;
    memcpy(d->ch1, f->y[0], sizeof(d->ch1));
    memcpy(d->ch2, f->y[1], sizeof(d->ch2));
    d->screen.ch1_samples = d->ch1;
    d->screen.ch2_samples = d->ch2;
    d->screen.ch1_min_samples = f->y_min[0];
    d->screen.ch1_max_samples = f->y_max[0];
    d->screen.ch2_min_samples = f->y_min[1];
    d->screen.ch2_max_samples = f->y_max[1];
    memcpy(d->fft_ch1, f->fft[0], sizeof(d->fft_ch1));
    memcpy(d->fft_ch2, f->fft[1], sizeof(d->fft_ch2));
    snprintf(d->fft_span_text, sizeof(d->fft_span_text), "%.1f MHz", f->sample_rate_hz / 2e6);
    d->screen.fft_ch1_bins = d->fft_ch1;
    d->screen.fft_ch2_bins = d->fft_ch2;
    d->screen.fft_span = d->fft_span_text;
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
            snprintf(text, size, "%.2f V", value);
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
