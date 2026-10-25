#ifdef NDEBUG
#undef NDEBUG
#endif
#include "signal_filter.h"
#include "capture_pipeline.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void kernel_test(void)
{
    double source[257], result[257];
    unsigned level, i;
    for (level = 0; level < 4; ++level) {
        for (i = 0; i < 257; ++i) source[i] = -0.25;
        signal_filter_apply(source, 257, level, result);
        for (i = 0; i < 257; ++i) assert(result[i] == -0.25);
        memset(source, 0, sizeof(source)); source[128] = 1;
        signal_filter_apply(source, 257, level, result);
        {
            double sum = 0;
            for (i = 0; i < 257; ++i) {
                assert(result[i] >= 0 && result[i] <= 1);
                assert(result[i] == result[256-i]); /* No phase shift or ringing. */
                sum += result[i];
            }
            assert(fabs(sum - 1) < 1e-12);
        }
        for (i = 0; i < 257; ++i) source[i] = i < 128 ? -1 : 1;
        signal_filter_apply(source, 257, level, result);
        for (i = 1; i < 257; ++i) assert(result[i] >= result[i-1]);
        source[50] = NAN;
        signal_filter_apply(source, 257, level, result);
        assert(isnan(result[50]) && isfinite(result[49]) && isfinite(result[51]));
    }
}

static void denoise_test(void)
{
    double source[4096],result[4096],manual[4096],input_error=0,output_error=0;
    unsigned random=7;
    for(unsigned i=0;i<4096;++i){random=random*1664525u+1013904223u;source[i]=(i<2048?-1:1)+((int)(random>>24)-127.5)/512;}
    signal_filter_denoise(source,4096,.1444,result);
    for(unsigned i=32;i<4064;++i)if(i<2028 || i>2068){
        double expected=i<2048?-1:1;
        input_error+=(source[i]-expected)*(source[i]-expected);output_error+=(result[i]-expected)*(result[i]-expected);
    }
    assert(output_error<input_error*.25);
    assert(result[2047]==source[2047] && result[2048]==source[2048]);
    signal_filter_apply(result,4096,3,manual);assert(manual[2047]>-.2 && manual[2048]<.2);
    signal_filter_denoise(source,4096,0,result);assert(!memcmp(source,result,sizeof(source)));
    source[20]=NAN;signal_filter_denoise(source,4096,.1444,result);assert(isnan(result[20]));
}

static double plateau_error(const DisplayFrame *frame)
{
    unsigned x, count = 0;
    double error = 0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
        unsigned sample = x * 4096 / SCOPE_PLOT_WIDTH;
        unsigned phase = sample % 1024;
        double expected, delta;
        if (phase < 64 || (phase > 448 && phase < 576) || phase > 960) continue;
        expected = phase < 512 ? 240 - 48 * 2 : 240 + 48 * 2;
        delta = frame->y[0][x] - expected;
        error += delta * delta;
        ++count;
    }
    return sqrt(error / count);
}

/* Exercise the ADC renderer used by the board: density, connected segments,
   peak metadata, per-channel selection and repeated views of one frozen record. */
static void processor_test(void)
{
    uint8_t data[8192], original[8192], peaks[8192];
    CaptureBuffer buffer = {0}; CaptureView view = {0};
    static DisplayFrame raw, filtered;
    unsigned i, level, random = 1;
    double previous;
    buffer.data = data; buffer.bytes = buffer.capacity = sizeof(data);
    buffer.sample_rate_hz = 200000000; buffer.sequence = 47;
    buffer.trigger_sample = CAPTURE_NO_TRIGGER;
    buffer.zero_code[0] = buffer.zero_code[1] = 128;
    buffer.volts_per_code[0] = buffer.volts_per_code[1] = .01;
    for (i = 0; i < 4096; ++i) {
        int base = i % 1024 < 512 ? 176 : 80;
        random = random * 1664525U + 1013904223U;
        data[2*i] = (uint8_t)(base + (int)((random >> 16) % 61) - 30);
        data[2*i+1] = (uint8_t)base;
    }
    memcpy(original, data, sizeof(data));
    view.samples = 4096; view.zero_y[0] = view.zero_y[1] = 240;
    view.pixels_per_volt[0] = view.pixels_per_volt[1] = 200;
    view.density_enabled = 1;
    assert(capture_process(&buffer, &view, &raw));
    previous = plateau_error(&raw);
    for (level = 1; level < 4; ++level) {
        double error;
        view.filter_level[0] = level;
        assert(capture_process(&buffer, &view, &filtered));
        error = plateau_error(&filtered);
        assert(error < previous * .8); previous = error;
        assert(!memcmp(raw.minimum, filtered.minimum, sizeof(raw.minimum)));
        assert(!memcmp(raw.maximum, filtered.maximum, sizeof(raw.maximum)));
        assert(!memcmp(raw.mean, filtered.mean, sizeof(raw.mean)));
        assert(!memcmp(raw.rms, filtered.rms, sizeof(raw.rms)));
        assert(!memcmp(raw.frequency_hz, filtered.frequency_hz, sizeof(raw.frequency_hz)));
        assert(!memcmp(raw.fft, filtered.fft, sizeof(raw.fft)));
        assert(!memcmp(raw.y[1], filtered.y[1], sizeof(raw.y[1])));
        assert(!memcmp(raw.connections[1], filtered.connections[1], sizeof(raw.connections[1])));
        assert(memcmp(raw.connections[0], filtered.connections[0], sizeof(raw.connections[0])));
        assert(memcmp(raw.density[0], filtered.density[0], sizeof(raw.density[0])));
        assert(!memcmp(original, data, sizeof(data)) && filtered.sequence == 47);
    }
    assert(previous < plateau_error(&raw) * .2);
    view.filter_level[0]=0;view.noise_rms[0]=.1761;
    assert(capture_process(&buffer,&view,&filtered));
    assert(plateau_error(&filtered)<plateau_error(&raw)*.7);
    assert(!memcmp(raw.fft,filtered.fft,sizeof(raw.fft)) && !memcmp(raw.rms,filtered.rms,sizeof(raw.rms)));
    assert(!memcmp(raw.y[1],filtered.y[1],sizeof(raw.y[1])) && !memcmp(original,data,sizeof(data)));
    view.noise_rms[0]=0;view.filter_level[0]=3;
    /* Unknown native extrema must not repaint noise over the filtered curve. */
    for (i = 0; i < sizeof(peaks); i += 4) {
        peaks[i] = peaks[i+1] = 0; peaks[i+2] = peaks[i+3] = 255;
    }
    buffer.peaks = peaks;
    assert(capture_process(&buffer, &view, &filtered));
    for (i = 0; i < SCOPE_PLOT_WIDTH; ++i) assert(!filtered.peak_strength[0][i]);
    view.filter_level[0]=0;view.noise_rms[0]=.1761;
    assert(capture_process(&buffer,&view,&filtered));
    for(i=0;i<SCOPE_PLOT_WIDTH;++i)assert(filtered.peak_strength[0][i]); /* Strong unresolved peaks survive AUTO. */
    view.noise_rms[0]=0;view.filter_level[0]=3;
    buffer.peaks = NULL;
    view.filter_level[0] = 0;
    assert(capture_process(&buffer, &view, &filtered));
    assert(!memcmp(raw.y, filtered.y, sizeof(raw.y)));
    assert(!memcmp(raw.connections, filtered.connections, sizeof(raw.connections)));
    assert(!memcmp(raw.density, filtered.density, sizeof(raw.density)));
    /* Zoom reads the same filtered source, rather than filtering pixel columns. */
    view.filter_level[0] = 3;
    view.display_first = 128; view.display_samples = 256;
    assert(capture_process(&buffer, &view, &filtered));
    {
        double source[4096], smoothed[4096];
        for (i = 0; i < 4096; ++i) source[i] = (data[2*i] - 128) * .01;
        signal_filter_apply(source, 4096, 3, smoothed);
        for (i = 0; i < SCOPE_PLOT_WIDTH; i += 4) {
            size_t sample = 128 + i * 256 / SCOPE_PLOT_WIDTH;
            assert(filtered.y[0][i] == (int16_t)(240 - smoothed[sample] * 200));
        }
    }
    assert(!memcmp(original, data, sizeof(data)));
}

static int next_frame(CapturePipeline *pipeline, DisplayFrame *frame)
{
    unsigned i;
    for (i = 0; i < 200; ++i) {
        if (capture_pipeline_frame(pipeline, frame)) return 1;
        scope_sleep_ms(5);
    }
    return 0;
}

/* A processing-only revision must rebuild STOP without acquiring a new frame. */
static void stopped_pipeline_test(void)
{
    CapturePipeline *pipeline = capture_pipeline_create(NULL);
    CaptureRequest request = {0};
    static DisplayFrame raw, filtered;
    CaptureMetrics before, after;
    assert(pipeline);
    request.duration_seconds = .00001; request.pan_fraction = .5;
    request.zero_y[0] = request.zero_y[1] = 240;
    request.pixels_per_volt[0] = request.pixels_per_volt[1] = 120;
    request.running = 1;
    capture_pipeline_request(pipeline, &request);
    assert(next_frame(pipeline, &raw));
    request.running = 0;
    capture_pipeline_request(pipeline, &request);
    scope_sleep_ms(100);
    while (capture_pipeline_frame(pipeline, &raw)) {}
    request.pan_fraction = .4;
    capture_pipeline_request(pipeline, &request);
    assert(next_frame(pipeline, &raw));
    capture_pipeline_metrics(pipeline, &before);
    request.filter_level[0] = 3;
    capture_pipeline_request(pipeline, &request);
    assert(next_frame(pipeline, &filtered));
    assert(filtered.sequence == raw.sequence);
    assert(memcmp(raw.y[0], filtered.y[0], sizeof(raw.y[0])));
    assert(!memcmp(raw.y[1], filtered.y[1], sizeof(raw.y[1])));
    request.filter_level[0] = 0;
    capture_pipeline_request(pipeline, &request);
    assert(next_frame(pipeline, &filtered));
    assert(!memcmp(raw.y, filtered.y, sizeof(raw.y)));
    capture_pipeline_metrics(pipeline, &after);
    assert(before.blocks == after.blocks);
    capture_pipeline_destroy(pipeline);
}

int main(void)
{
    kernel_test(); denoise_test(); processor_test(); stopped_pipeline_test();
    return 0;
}
