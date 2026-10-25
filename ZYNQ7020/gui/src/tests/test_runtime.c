#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_file.h"
#include "capture_pipeline.h"
#include "spectrum_bins.h"
#include "control_transport.h"
#include "waveform_view.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void protocol_test(void)
{
    ControlParser p;
    ControlEvent e = {CONTROL_ROTATE, 0, 65535, UINT32_MAX, -17}, out;
    uint8_t packet[CONTROL_PACKET_SIZE];
    unsigned i;
    int got = 0;
    assert(control_encode(packet, &e));
    control_parser_init(&p);
    assert(control_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    control_parser_feed(&p, 0xa5, &out);
    control_parser_feed(&p, 0, &out);
    for (i = 0; i < sizeof(packet); ++i)
        got += control_parser_feed(&p, packet[i], &out);
    assert(got == 1 && out.value == -17 && out.timestamp_ms == UINT32_MAX);
    for (i = 0; i < sizeof(packet); ++i)
        assert(!control_parser_feed(&p, packet[i], &out));
    assert(p.duplicates == 1);
    e.sequence = 0;
    assert(control_encode(packet, &e));
    for (i = 0; i < sizeof(packet); ++i)
        control_parser_feed(&p, packet[i], &out);
    assert(p.packets == 2 && p.sequence_gaps == 0);
    packet[12] ^= 1;
    for (i = 0; i < sizeof(packet); ++i)
        assert(!control_parser_feed(&p, packet[i], &out));
    assert(p.crc_errors == 1);
    e.sequence = 3;
    assert(control_encode(packet, &e));
    for (i = 0; i < sizeof(packet); ++i)
        control_parser_feed(&p, packet[i], &out);
    assert(p.packets == 3 && p.sequence_gaps == 2);
    e.control = 9;
    assert(!control_encode(packet, &e));
    e.type=CONTROL_TEMPERATURE;e.control=255;
    const int values[]={-40000,0,125000,CONTROL_TEMPERATURE_UNAVAILABLE};
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i){
        e.value=values[i];++e.sequence;got=0;
        assert(control_encode(packet,&e));
        for(unsigned j=0;j<sizeof(packet);++j)got+=control_parser_feed(&p,packet[j],&out);
        assert(got==1 && out.type==CONTROL_TEMPERATURE && out.value==values[i]);
    }
    e.value=-40001;assert(!control_encode(packet,&e));
    e.value=125001;assert(!control_encode(packet,&e));
    e.value=25000;e.control=0;assert(!control_encode(packet,&e));
    for(unsigned type=CONTROL_SET_SCALE;type<=CONTROL_SCALE;++type){
        e.type=(ControlEventType)type;e.control=0;e.value=CONTROL_SCALE_OFF;++e.sequence;got=0;
        assert(control_encode(packet,&e));
        for(i=0;i<sizeof(packet);++i)got+=control_parser_feed(&p,packet[i],&out);
        assert(got==1 && out.value==CONTROL_SCALE_OFF && out.type==e.type);
        e.value=-1;assert(!control_encode(packet,&e));
        e.value=CONTROL_SCALE_OFF;e.control=2;assert(!control_encode(packet,&e));
    }
}
static void queue_test(void)
{
    ControlQueue q;
    ControlEvent e = {CONTROL_DOWN, 0, 0, 0, 0}, out;
    unsigned i;
    control_queue_init(&q);
    for (i = 0; i < CONTROL_QUEUE_SIZE; ++i)
        assert(control_queue_push(&q, &e));
    assert(!control_queue_push(&q, &e));
    assert(q.dropped == CONTROL_QUEUE_SIZE + 1 && control_queue_generation(&q) == 1);
    assert(!control_queue_pop(&q, &out));
    control_queue_destroy(&q);
}
static void fill(CaptureBuffer *b, unsigned seq)
{
    b->bytes = b->capacity;
    b->sequence = seq;
    b->sample_rate_hz = 200000000;
    b->trigger_sample = CAPTURE_NO_TRIGGER;
    b->zero_code[0] = b->zero_code[1] = 128;
    b->volts_per_code[0] = b->volts_per_code[1] = 0.01;
    memset(b->data, 128, b->bytes);
}
static void ring_test(void)
{
    CaptureRing r;
    uint8_t data[2][16], *storage[] = {data[0], data[1]};
    int a, b, c;
    assert(capture_ring_init(&r, storage, 2, 16, CAPTURE_OVERWRITE_READY));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 1);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_take(&r, 0) == a);
    b = capture_ring_begin(&r);
    fill(&r.buffer[b], 2);
    assert(capture_ring_publish(&r, b));
    c = capture_ring_begin(&r);
    assert(c == b && r.overwritten == 1);
    fill(&r.buffer[c], 3);
    assert(capture_ring_publish(&r, c));
    assert(capture_ring_take(&r, 0) == b);
    assert(capture_ring_begin(&r) < 0);
    assert(r.buffer[a].sequence == 1);
    capture_ring_release(&r, a);
    capture_ring_release(&r, b);
    capture_ring_destroy(&r);
    assert(capture_ring_init(&r, storage, 1, 16, CAPTURE_STOP_ON_FULL));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 0);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_begin(&r) < 0 && r.stopped);
    capture_ring_destroy(&r);
    assert(capture_ring_init(&r, storage, 1, 16, CAPTURE_DROP_NEW));
    a = capture_ring_begin(&r);
    fill(&r.buffer[a], 0);
    assert(capture_ring_publish(&r, a));
    assert(capture_ring_begin(&r) < 0 && !r.stopped && r.dropped == 1);
    capture_ring_destroy(&r);
    {
        uint8_t large[CAPTURE_RING_MAX][16], *blocks[CAPTURE_RING_MAX];
        size_t i;
        for (i = 0; i < CAPTURE_RING_MAX; ++i)
            blocks[i] = large[i];
        assert(!capture_ring_init(&r, blocks, CAPTURE_RING_MAX + 1, 16, CAPTURE_DROP_NEW));
        assert(capture_ring_init(&r, blocks, CAPTURE_RING_MAX, 16, CAPTURE_DROP_NEW));
        for (i = 0; i < CAPTURE_RING_MAX; ++i) {
            a = capture_ring_begin(&r);
            assert(a >= 0);
            fill(&r.buffer[a], (unsigned)i);
            assert(capture_ring_publish(&r, a));
        }
        assert(r.occupancy == CAPTURE_RING_MAX && capture_ring_begin(&r) < 0);
        for (i = 0; i < CAPTURE_RING_MAX; ++i) {
            a = capture_ring_take(&r, 0);
            assert(a >= 0 && r.buffer[a].sequence == i);
            capture_ring_release(&r, a);
        }
        capture_ring_destroy(&r);
    }
}
static void processor_file_test(void)
{
    uint8_t data[8192] = {0}, loaded[8192];
    CaptureBuffer b = {0}, read = {0};
    CaptureView view = {0, 4096, {200, 330}, {100, 100}};
    static DisplayFrame frame;
    CaptureSource source;
    FileCaptureSource context;
    FILE *file = tmpfile();
    double volts;
    b.data = data;
    b.capacity = sizeof(data);
    fill(&b, 17);
    data[2 * 130] = 255;
    data[2 * 131] = 0;
    assert(capture_process(&b, &view, &frame));
    assert(frame.y_min[0][32] < 100 && frame.y_max[0][32] > 300);
    assert(frame.maximum[0] == 1.27 && frame.minimum[0] == -1.28);
    view.start = 128;
    view.samples = 16;
    assert(capture_process(&b, &view, &frame));
    assert(capture_value_at(&b, 130, 0, &volts) && fabs(volts - 1.27) < 1e-12);
    assert(file && capture_file_write(file, &b));
    rewind(file);
    assert(file_capture_source_init(&source, &context, file));
    read.data = loaded;
    read.capacity = sizeof(loaded);
    assert(source.read(&source, &read) == 1);
    assert(!memcmp(data, loaded, sizeof(data)) && read.sequence == 17);
    assert(source.read(&source, &read) == 0);
    rewind(file);
    assert(file_capture_source_init(&source, &context, file));
    assert(fseek(file, 128, SEEK_SET) == 0);
    fputc(0, file);
    assert(fseek(file, 128, SEEK_SET) == 0);
    assert(source.read(&source, &read) == -1);
    fclose(file);
}

static void processor_record_bounds_test(void)
{
    uint8_t data[8192];CaptureBuffer b={0};static DisplayFrame f;
    CaptureView view={0,0,{200,330},{100,100},-2048,8192};
    b.data=data;b.capacity=sizeof(data);fill(&b,77);
    b.accepted_ns=123456789;
    data[2*2048]=255;
    assert(capture_process(&b,&view,&f));
    assert(f.sequence==77 && f.view_start==0 && f.view_samples==4096);
    assert(f.capture_id==b.accepted_ns);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x){
        if(x<256 || x>=768)assert(f.y[0][x]==WAVEFORM_NO_SAMPLE && f.y_min[0][x]==WAVEFORM_NO_SAMPLE);
        else assert(f.y[0][x]!=WAVEFORM_NO_SAMPLE);
    }
    assert(f.y_min[0][512]==73); /* Pulse stays at the physical midpoint. */
    view.display_first=-8192;
    assert(capture_process(&b,&view,&f) && !f.view_samples && isnan(f.mean[0]));
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(f.y[0][x]==WAVEFORM_NO_SAMPLE);
    view.display_first=4096;
    assert(capture_process(&b,&view,&f) && !f.view_samples);
    view.display_first=-0.5;view.display_samples=2;
    assert(capture_process(&b,&view,&f));
    assert(f.y[0][0]==WAVEFORM_NO_SAMPLE && f.y[0][256]==200);
    view.display_samples=NAN;assert(!capture_process(&b,&view,&f));
    view.display_samples=8192;view.display_first=INFINITY;assert(!capture_process(&b,&view,&f));
    b.trigger_sample=b.first_sample+2048;
    view.display_first=0;view.display_samples=4096;
    assert(capture_process(&b,&view,&f)&&fabs(f.trigger_x-512)<1e-9);
    view.display_first=512;
    assert(capture_process(&b,&view,&f)&&fabs(f.trigger_x-384)<1e-9);
    view.display_first=1024;view.display_samples=2048;
    assert(capture_process(&b,&view,&f)&&fabs(f.trigger_x-512)<1e-9);
    view.skip_spectrum=1;
    assert(capture_process(&b,&view,&f));
    for(unsigned x=0;x<SCOPE_FFT_BINS;++x)assert(!f.fft[0][x]&&!f.fft[1][x]);
}

static void processor_short_pulse_test(void)
{
    static const size_t lengths[] = {4093,32765};
    uint8_t data[65536];
    CaptureBuffer buffer = {0};
    CaptureView view = {1, 4093, {200, 330}, {100, 100}};
    static DisplayFrame frame;
    size_t i, x, length;
    buffer.data = data;
    buffer.capacity = sizeof(data);
    fill(&buffer, 1);
    for (length = 0; length < 2; ++length) {
        view.samples = lengths[length];
        size_t positions[] = {0,1,3,4,15,16,31,32,127,128,view.samples-2,view.samples-1};
        for (i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
            size_t pulse = positions[i];
            size_t column = ((pulse + 1) * SCOPE_PLOT_WIDTH - 1) / view.samples;
            memset(data, 128, sizeof(data));
            data[2 * (view.start + pulse)] = 255;
            assert(capture_process(&buffer, &view, &frame));
            for (x = 0; x < SCOPE_PLOT_WIDTH; ++x) {
                assert(frame.y_min[0][x] == (x == column ? 73 : 200));
                assert(frame.y_max[0][x] == 200);
            }
        }
    }
}
static void processor_zoom_source_test(void)
{
    uint8_t data[8192], original[8192];
    CaptureBuffer buffer = {0};
    CaptureView view = {0, 4096, {200, 330}, {100, 100}};
    static DisplayFrame frame;
    size_t x;
    buffer.data = data;
    buffer.capacity = sizeof(data);
    fill(&buffer, 1);
    memset(data, 128, sizeof(data));
    data[2 * 2048] = 255;
    memcpy(original, data, sizeof(data));
    assert(capture_process(&buffer, &view, &frame));
    assert(frame.y_min[0][512] == 73 && frame.y_max[0][512] == 200);
    view.start = 2047;
    view.samples = 4;
    assert(capture_process(&buffer, &view, &frame));
    assert(frame.y[0][0] == 200 && frame.y[0][256] == 73 && frame.y[0][512] == 200);
    assert(frame.y[0][128] == 136); /* Between original samples, not compressed min/max. */
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        assert(frame.y_min[0][x] == frame.y_max[0][x]);
    assert(!memcmp(original, data, sizeof(data)) && frame.sequence == 1);
}

static void processor_statistics_test(void)
{
    uint8_t data[512];
    CaptureBuffer b={0};CaptureView view={0,256,{200,330},{100,100}};static DisplayFrame f;
    b.data=data;b.capacity=sizeof(data);fill(&b,1);view.skip_spectrum=1;
    for(unsigned i=0;i<256;++i){data[2*i]=(uint8_t)i;data[2*i+1]=(uint8_t)(255-i);}
    for(int sign=-1;sign<=1;++sign){
        b.volts_per_code[0]=sign*0.02;b.zero_code[0]=127.25;
        double sum=0,squares=0,low=INFINITY,high= -INFINITY;
        for(unsigned i=0;i<256;++i){
            double v=(i-127.25)*b.volts_per_code[0];
            sum+=v;squares+=v*v;if(v<low)low=v;if(v>high)high=v;
        }
        assert(capture_process(&b,&view,&f));
        assert(fabs(f.minimum[0]-low)<1e-12&&fabs(f.maximum[0]-high)<1e-12);
        assert(fabs(f.mean[0]-sum/256)<1e-12&&fabs(f.rms[0]-sqrt(squares/256))<1e-12);
        assert(f.duty[0]==(sign?50:100)&&f.frequency_hz[0]==0);
    }
}

static void processor_large_statistics_test(void)
{
    const size_t count=1000003;
    CaptureBuffer b={0};CaptureView view={0};static DisplayFrame f;
    uint8_t *data=malloc((count+2)*2);assert(data);
    b.data=data;b.capacity=(count+2)*2;fill(&b,1);
    view.start=1;view.samples=count;view.skip_spectrum=1;
    for(size_t i=0;i<count+2;++i)for(unsigned ch=0;ch<2;++ch)
        data[2*i+ch]=i<65539?255:(uint8_t)generator_noise_code(37,i,ch);
    for(int sign=-1;sign<=1;++sign){
        b.volts_per_code[0]=sign*0.02;b.zero_code[0]=127.25;
        uint64_t sum=0,squares=0;size_t high=0,crossings=0,first=0,last=0;
        int previous=sign>=0;
        for(size_t i=0;i<count;++i){
            unsigned code=data[2*(i+1)];
            int level=sign==0||(sign>0?code>=128:code<=127);
            sum+=code;squares+=code*code;high+=level;
            if(level&&!previous){if(!crossings)first=i;last=i;++crossings;}
            previous=level;
        }
        assert(capture_process(&b,&view,&f));
        double mean=(double)sum/count,zero=b.zero_code[0],gain=b.volts_per_code[0];
        double rms=sqrt((double)squares/count-2*zero*mean+zero*zero)*fabs(gain);
        assert(fabs(f.mean[0]-(mean-zero)*gain)<1e-12&&fabs(f.rms[0]-rms)<1e-12);
        assert(fabs(f.duty[0]-100.0*high/count)<1e-12);
        assert(fabs(f.frequency_hz[0]-(crossings>1?(double)(crossings-1)*b.sample_rate_hz/(last-first):0))<1e-9);
        assert(fabs(f.minimum[0]-(gain<0?255-zero:-zero)*gain)<1e-12);
        assert(fabs(f.maximum[0]-(gain<0?-zero:255-zero)*gain)<1e-12);
    }
    free(data);
}
static void processor_fft_test(void)
{
    static const unsigned frequencies[] = {10, 201, 400, 511, 512};
    uint8_t data[2048];
    CaptureBuffer buffer = {0};
    CaptureView view = {0, 1024, {200, 330}, {100, 100}};
    static DisplayFrame frame;
    unsigned n, i, k;
    unsigned coverage[513] = {0};
    buffer.data = data;
    buffer.capacity = sizeof(data);
    fill(&buffer, 1);
    for (i = 0; i < SCOPE_FFT_BINS; ++i) {
        unsigned first, end;
        spectrum_bin_range(i, SCOPE_FFT_BINS, 1024, &first, &end);
        assert(end > first && end <= 513);
        for (k = first; k < end; ++k) coverage[k] = 1;
    }
    for (k = 1; k <= 512; ++k) assert(coverage[k]);
    for (n = 0; n < sizeof(frequencies) / sizeof(frequencies[0]); ++n) {
        unsigned peak = 0, expected = 0;
        for (i = 0; i < 1024; ++i) {
            data[2 * i] = (uint8_t)lround(192 + 50 * cos(6.283185307179586 * frequencies[n] * i / 1024));
            data[2 * i + 1] = 128;
        }
        assert(capture_process(&buffer, &view, &frame));
        for (k = 0; k < SCOPE_FFT_BINS; ++k) {
            unsigned first, end;
            spectrum_bin_range(k, SCOPE_FFT_BINS, 1024, &first, &end);
            if (frequencies[n] >= first && frequencies[n] < end) expected = k;
            if (frame.fft[0][k] > frame.fft[0][peak]) peak = k;
        }
        assert(fabs(spectrum_frequency_at((double)peak/(SCOPE_FFT_BINS-1),512,1024)-frequencies[n])<3 && frame.fft[0][peak] > 200);
    }
    {
        size_t samples = 100000;
        unsigned peak = 0;
        uint8_t *long_data = malloc(samples * 2);
        assert(long_data);
        for (i = 0; i < samples; ++i) {
            long_data[2*i] = (uint8_t)lround(128 + 50*cos(6.283185307179586*10000*i/50000000));
            long_data[2*i+1] = 128;
        }
        buffer.data = long_data; buffer.bytes = buffer.capacity = samples * 2;
        buffer.sample_rate_hz = 50000000; view.samples = samples;
        assert(capture_process(&buffer, &view, &frame));
        assert(frame.fft_size == 65536);
        for (k = 1; k < SCOPE_FFT_BINS; ++k)
            if (frame.fft[0][k] > frame.fft[0][peak]) peak = k;
        assert(fabs(spectrum_frequency_at((double)peak/(SCOPE_FFT_BINS-1),25000000,frame.fft_size)-10000)<1000);
        assert(frame.fft[0][peak] > 200);
        assert(spectrum_frequency_at(0,25000000,frame.fft_size) == 0);
        samples=1000000;
        long_data=realloc(long_data,samples*2);assert(long_data);
        for(i=100000;i<samples;++i){
            long_data[2*i]=(uint8_t)lround(128+50*cos(6.283185307179586*10000*i/50000000));
            long_data[2*i+1]=128;
        }
        buffer.data=long_data;buffer.bytes=buffer.capacity=samples*2;view.samples=samples;
        assert(capture_process(&buffer,&view,&frame));
        assert(frame.fft_size==524288 && fabs(frame.fundamental_hz[0]-10000)<3);
        free(long_data);
    }
}

static void pipeline_lease_test(void)
{
    CapturePipeline *p=capture_pipeline_create(NULL);
    const DisplayFrame *held=NULL;DisplayFrame *copy=malloc(sizeof(*copy));CaptureMetrics before,after;
    assert(p && copy);
    for(unsigned i=0;i<200 && !held;++i){held=capture_pipeline_acquire(p);scope_sleep_ms(5);}
    assert(held);capture_pipeline_retain_frame(p,held);
    memcpy(copy,held,sizeof(*copy));capture_pipeline_metrics(p,&before);
    for(unsigned i=0;i<200;++i){capture_pipeline_metrics(p,&after);if(after.waveforms>before.waveforms+1)break;scope_sleep_ms(5);}
    assert(after.waveforms>before.waveforms+1 && !memcmp(copy,held,sizeof(*copy)));
    assert(capture_pipeline_acquire(p));
    scope_sleep_ms(100);assert(!memcmp(copy,held,sizeof(*copy)));
    capture_pipeline_release_frame(p,held);capture_pipeline_destroy(p);free(copy);
}

static void pipeline_test(void)
{
    CapturePipeline *p = capture_pipeline_create(NULL);
    static DisplayFrame f;
    CaptureMetrics m;
    unsigned i;
    int got = 0;
    CaptureRequest request = {0.001, 0.5, {200, 330}, {120, 60}, 1};
    assert(p);
    for (i = 0; i < 200 && !got; ++i) {
        got = capture_pipeline_frame(p, &f);
        scope_sleep_ms(5);
    }
    assert(got && f.sample_rate_hz == 200000000);
    request.running = 0;
    capture_pipeline_request(p, &request);
    scope_sleep_ms(100);
    capture_pipeline_metrics(p, &m);
    assert(m.blocks > 0 && !m.source_error);
    {
        uint64_t before = m.blocks;
        scope_sleep_ms(100);
        capture_pipeline_metrics(p, &m);
        assert(m.blocks == before);
    }
    request.duration_seconds = 0.0001;
    capture_pipeline_request(p, &request);
    got = 0;
    for (i = 0; i < 200; ++i) {
        if (capture_pipeline_frame(p, &f) && f.view_samples == 20000) {
            got = 1;
            break;
        }
        scope_sleep_ms(5);
    }
    assert(got);
    {
        uint64_t sequence=f.sequence, blocks=m.blocks;
        request.duration_seconds=0.02;capture_pipeline_request(p,&request);got=0;
        for(i=0;i<200;++i){
            if(capture_pipeline_frame(p,&f) && f.y[0][0]==WAVEFORM_NO_SAMPLE){got=1;break;}
            scope_sleep_ms(5);
        }
        assert(got && f.sequence==sequence && f.y[0][512]!=WAVEFORM_NO_SAMPLE);
        capture_pipeline_metrics(p,&m);assert(m.blocks==blocks);
    }
    capture_pipeline_destroy(p);
}
static void capture_debug_test(void)
{
    uint8_t raw[4096], saved[4096], generated[4096];
    CaptureBuffer input={0}, output={0};
    CaptureSource source;
    DemoCaptureSource context;
    CaptureDebug debug={1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},1000000};
    unsigned wave;
    uint64_t trigger;
    input.data=raw;input.capacity=sizeof(raw);
    demo_capture_source_init(&source,&context,200000000);
    assert(source.read(&source,&input)==1);
    input.sequence=42;input.first_sample=123000;input.hardware_ns=615000;
    input.flags=1;input.trigger_sample=input.first_sample+123;
    memcpy(saved,raw,sizeof(raw));
    output.data=generated;output.capacity=sizeof(generated);
    for(wave=0;wave<SIGNAL_WAVE_COUNT;++wave){
        debug.wave[0]=wave;
        assert(capture_debug_replace(&input,&output,&debug));
        assert(output.sequence==42&&output.first_sample==123000&&output.hardware_ns==615000);
        assert(output.flags==input.flags&&output.trigger_sample==input.trigger_sample);
        assert(!memcmp(saved,raw,sizeof(raw))&&memcmp(raw,generated,sizeof(raw)));
    }
    /* Even an aliased constant carrier has fresh, bounded sample noise.
       Reprocessing the same completed block must remain deterministic. */
    debug.wave[0]=SIGNAL_WAVE_SQUARE;debug.wave[1]=SIGNAL_WAVE_SINE;
    debug.frequency_hz=input.sample_rate_hz;
    assert(capture_debug_replace(&input,&output,&debug));
    {
        unsigned different[2]={0};
        for(size_t i=0;i<input.bytes/2;++i) {
            assert(generated[2*i]>=127&&generated[2*i]<=129);
            assert(generated[2*i+1]>=205&&generated[2*i+1]<=207);
            for(unsigned ch=0;ch<2;++ch)different[ch]+=generated[2*i+ch]!=generated[ch];
        }
        assert(different[0]>100&&different[1]>100);
    }
    memcpy(saved,generated,sizeof(saved));
    assert(capture_debug_replace(&input,&output,&debug)&&!memcmp(saved,generated,sizeof(saved)));
    ++input.sequence;
    assert(capture_debug_replace(&input,&output,&debug)&&memcmp(saved,generated,sizeof(saved)));
    input.bytes=6;input.trigger_sample=CAPTURE_NO_TRIGGER;
    memset(generated,0x5a,sizeof(generated));
    assert(capture_debug_replace(&input,&output,&debug));
    assert(generated[4]>=127&&generated[4]<=129&&generated[5]>=205&&generated[5]<=207);
    for(size_t i=6;i<sizeof(generated);++i)assert(generated[i]==0x5a);
    input.bytes=sizeof(raw);
    debug.wave[0]=SIGNAL_WAVE_NOISE;
    assert(capture_debug_replace(&input,&output,&debug));
    for(size_t i=0;i<input.bytes/2;++i) {
        assert(generated[2*i]==128+generator_noise_code((uint32_t)input.sequence,input.first_sample+i,0)%61);
        assert(generated[2*i+1]>=205&&generated[2*i+1]<=207);
    }
    debug.wave[0]=debug.wave[1]=SIGNAL_WAVE_NOISE;
    debug.offset_fraction[0]=0.17;debug.offset_fraction[1]=0.5;
    assert(capture_debug_replace(&input,&output,&debug));
    for(size_t i=0;i<input.bytes/2;++i)for(unsigned ch=0;ch<2;++ch)
        assert(generated[2*i+ch]==(uint8_t)(128-debug.offset_fraction[ch]*(ch?80:60)+
            generator_noise_code((uint32_t)input.sequence,input.first_sample+i,ch)%(ch?81:61)));
    output.capacity=sizeof(generated)-1;assert(!capture_debug_replace(&input,&output,&debug));
    output.capacity=sizeof(generated);debug.enabled=0;
    assert(!capture_debug_replace(&input,&output,&debug));
    memset(raw,128,sizeof(raw));input.trigger_sample=CAPTURE_NO_TRIGGER;
    input.first_sample=100;input.volts_per_code[0]=1;
    raw[4]=129;raw[6]=127;raw[10]=129;
    assert(capture_find_trigger(&input,0,0,0,0,100,&trigger)&&trigger==102);
    assert(capture_find_trigger(&input,0,1,0,0,100,&trigger)&&trigger==103);
    assert(capture_find_trigger(&input,0,0,0,0,103,&trigger)&&trigger==105);
    assert(!capture_find_trigger(&input,0,0,200,0,100,&trigger));
    {
        const uint8_t noisy[]={140,136,132,129,128,129,127,128,129,126,124,126,
                               128,129,132,136,140,136,132,129,128,129,127,124};
        input.bytes=sizeof(noisy)*2;
        for(size_t i=0;i<sizeof(noisy);++i)raw[2*i]=noisy[i];
        /* Without an arm margin, noise on the falling slope looks like RISING. */
        assert(capture_find_trigger(&input,0,0,0,0,100,&trigger)&&trigger==105);
        assert(capture_find_trigger(&input,0,0,0,2,100,&trigger)&&trigger==113);
        assert(capture_find_trigger(&input,0,1,0,2,100,&trigger)&&trigger==106);
        assert(capture_find_trigger(&input,0,2,0,2,100,&trigger)&&trigger==106);
        assert(capture_find_trigger(&input,0,0,0,2,106,&trigger)&&trigger==113);
        assert(capture_find_trigger(&input,0,1,0,2,107,&trigger)&&trigger==122);
    }
    {
        unsigned false_rising=0;
        input.bytes=sizeof(raw);input.sample_rate_hz=1000000;
        input.first_sample=100;input.trigger_sample=CAPTURE_NO_TRIGGER;
        debug=(CaptureDebug){1,{SIGNAL_WAVE_SINE,SIGNAL_WAVE_SINE},1000};
        output.capacity=sizeof(generated);
        /* Exercise the actual noisy DEBUG generator. Local threshold crossings
           on a falling sine must not be accepted as a rising carrier edge. */
        for(unsigned sequence=0;sequence<32;++sequence){
            input.sequence=sequence;
            assert(capture_debug_replace(&input,&output,&debug));
            assert(capture_find_trigger(&output,0,0,0.6,0,100,&trigger));
            if(cos(2*3.141592653589793*(trigger%1000)/1000.0)<0)++false_rising;
            assert(capture_find_trigger(&output,0,0,0.6,2,100,&trigger));
            assert(cos(2*3.141592653589793*(trigger%1000)/1000.0)>0);
            assert(capture_find_trigger(&output,0,1,0.6,2,100,&trigger));
            assert(cos(2*3.141592653589793*(trigger%1000)/1000.0)<0);
        }
        assert(false_rising>0);
    }
}
static void noisy_edge_test(void)
{
    uint8_t data[8192];CaptureBuffer b={0};uint64_t trigger;
    b.data=data;b.capacity=b.bytes=sizeof(data);b.sample_rate_hz=200000000;
    b.zero_code[0]=b.zero_code[1]=128;b.volts_per_code[0]=b.volts_per_code[1]=1;
    b.first_sample=1000;b.trigger_sample=CAPTURE_NO_TRIGGER;
    /* Slow square edges with independent bounded ADC jitter, well above the
       previous two-code rearm margin. Check both channels and all start phases. */
    for(unsigned seed=0;seed<32;++seed){
        uint32_t noise=seed+1;
        for(size_t i=0;i<b.bytes/2;++i){
            unsigned phase=(unsigned)(i%512);
            int carrier=phase<128?88:phase<256?88+(int)(phase-128)*80/128:
                        phase<384?168:168-(int)(phase-384)*80/128;
            for(unsigned ch=0;ch<2;++ch){noise=noise*1664525U+1013904223U;
                data[2*i+ch]=(uint8_t)(carrier+(int)(noise%17)-8);}
        }
        for(unsigned ch=0;ch<2;++ch)for(unsigned start=0;start<512;start+=7){
            for(unsigned edge=0;edge<3;++edge){
                assert(capture_find_trigger(&b,ch,edge,0,2,b.first_sample+start,&trigger));
                size_t index=(size_t)(trigger-b.first_sample);unsigned phase=(unsigned)(index%512);
                int rising=phase>=128 && phase<256;
                assert(rising || (phase>=384 && phase<512));
                assert(edge==2 || rising==(edge==0));
                assert(index>=start && index>0);
                if(rising)assert(data[2*(index-1)+ch]<=128 && data[2*index+ch]>128);
                else assert(data[2*(index-1)+ch]>=128 && data[2*index+ch]<128);
            }
        }
    }
    /* Trigger and trace use the same selected filter. A strong near-Nyquist
       disturbance must not reverse the visible, resolved carrier slope. */
    uint32_t noise=17;
    for(size_t i=0;i<b.bytes/2;++i){
        noise=noise*1664525U+1013904223U;
        data[2*i]=(uint8_t)lround(128+24*sin(6.283185307179586*i/100.)+
            18*cos(6.283185307179586*.49*i)+(int)(noise%17)-8);
    }
    uint8_t saved[sizeof(data)];memcpy(saved,data,sizeof(data));
    for(unsigned start=32;start<512;start+=7)for(unsigned edge=0;edge<2;++edge){
        assert(capture_find_trigger_filtered(&b,0,edge,4.1,2,2,0,b.first_sample+start,&trigger));
        double slope=cos(6.283185307179586*(trigger-b.first_sample)/100.);
        assert(edge==0?slope>0:slope<0);
    }
    assert(!memcmp(saved,data,sizeof(data)));
    /* A threshold excursion that returns to its original side is not an edge;
       qualification retains the crossing time rather than shifting to the guard. */
    const uint8_t bounce[]={110,125,130,129,126,110,125,129,135,140,145,140,
                             135,127,129,140,135,127,120,110};
    b.bytes=sizeof(bounce)*2;
    for(size_t i=0;i<sizeof(bounce);++i)data[2*i]=bounce[i];
    assert(capture_find_trigger(&b,0,0,0,4,1000,&trigger) && trigger==1007);
    assert(capture_find_trigger(&b,0,1,0,4,1000,&trigger) && trigger==1017);
    assert(!capture_find_trigger(&b,0,0,0,4,1008,&trigger));
    /* Fully resolved short-period carriers must keep both directions eligible. */
    b.bytes=b.capacity;
    for(unsigned period=2;period<=32;period*=2){
        for(size_t i=0;i<b.bytes/2;++i)data[2*i]=(uint8_t)(i%period<period/2?80:176);
        assert(capture_find_trigger(&b,0,0,0,2,1000,&trigger));
        assert(data[2*(trigger-1000)]==176 && data[2*(trigger-1001)]==80);
        assert(capture_find_trigger(&b,0,1,0,2,1000,&trigger));
        assert(data[2*(trigger-1000)]==80 && data[2*(trigger-1001)]==176);
    }
}

static void processor_density_test(void)
{
    uint8_t data[16384];
    CaptureBuffer b={0};
    CaptureView v={0,8192,{200,330},{100,100}};
    static DisplayFrame frame;
    unsigned i;
    b.data=data;b.capacity=sizeof(data);fill(&b,1);
    for(i=0;i<8192;++i){data[2*i]=i%8==7?148:128;data[2*i+1]=128;}
    v.skip_spectrum=1;v.density_enabled=1;
    assert(capture_process(&b,&v,&frame) && frame.density_valid);
    assert(frame.density[0][300][200]==255);
    assert(frame.density[0][300][180]==16);
    assert(frame.density[0][300][190]==0); /* No invented hits inside min/max. */
    assert(frame.connections[0][300][190]>0); /* Real sample-to-sample connection, separate from hits. */
    v.display_first=2048;v.display_samples=1024;
    assert(capture_process(&b,&v,&frame) && frame.density_valid);
    assert(frame.density[0][300][200]==255);
    assert(frame.density[0][303][180]==255);
    assert(frame.density[0][300][190]==0);
    v.display_samples=0;
    assert(capture_process(&b,&v,&frame));
    assert(frame.density[0][300][200]==255 && frame.density[0][300][180]==16);
    for(i=0;i<8192;++i)data[2*i]=i%8==7?127:128;
    v.density_rows=240;
    assert(capture_process(&b,&v,&frame) && frame.density_rows==240);
    /* Logical rows 200 and 201 share a physical pixel: sum before coloring. */
    assert(frame.density[0][300][100]==255 && frame.density[0][300][101]==0);
    assert((&frame.connections[0][0][0])[300*240+100]);
    assert(!(&frame.connections[0][0][0])[300*240+101]);
}

static void connected_projection_test(void)
{
    uint8_t data[32768];
    static uint8_t coverage[1024*480];
    double positions[256];unsigned i,x,y;
    const unsigned spans[]={32768,20480,16384,8192};
    for(i=0;i<256;++i)positions[i]=100+i;
    /* Every column contains complete periods. Picking its first sample would
       produce DC or a false slow beat; all real transitions must form a wall. */
    for(i=0;i<sizeof(data);++i)data[i]=i%8<4?0:200;
    for(i=0;i<sizeof(spans)/sizeof(spans[0]);++i){
        assert(waveform_view_connect_u8(data,sizeof(data),1,positions,.25,spans[i]-1,
                                        1024,480,coverage));
        for(x=0;x<1024;++x){
            for(y=100;y<=300;++y)assert(coverage[x*480+y]);
            assert(!coverage[x*480+99] && !coverage[x*480+301]);
        }
    }
    /* The dominant trace stays at full brightness on either side of one
       sample per pixel and under compression; rare paths remain weaker. */
    memset(data,100,sizeof(data));
    {
        const double scales[]={512,1023,1024,1025,2048,8192,32768};
        unsigned scale;
        for(scale=0;scale<sizeof(scales)/sizeof(scales[0]);++scale){
            assert(waveform_view_connect_u8(data,sizeof(data),1,positions,0,scales[scale],
                                            1024,480,coverage));
            for(x=0;x<1024;++x)assert(coverage[x*480+200]==255);
        }
        data[7]=200;
        assert(waveform_view_connect_u8(data,sizeof(data),1,positions,0,8192,1024,480,coverage));
        assert(coverage[200]==255);
        assert(coverage[250]>=96 && coverage[250]<coverage[200]);
        assert(!coverage[301]);
    }
    /* A resolved period keeps its two crossings; drawing cannot invent a
       lower-frequency envelope when the view is shifted by a fractional sample. */
    for(i=0;i<sizeof(data);++i)data[i]=i%128<64?0:200;
    for(i=0;i<4;++i){
        unsigned crossings=0,previous=0;
        assert(waveform_view_connect_u8(data,sizeof(data),1,positions,i*.23,4096,
                                        1024,480,coverage));
        for(x=0;x<1024;++x){
            unsigned current=coverage[x*480+200]!=0;
            if(current && !previous)++crossings;
            previous=current;
        }
        assert(crossings==64);
    }
    /* Interpolated front at sparse zoom: every pixel along it is connected,
       but the native peak bucket never turns into a stretched rectangle. */
    data[0]=0;data[1]=200;
    assert(waveform_view_connect_u8(data,2,1,positions,0,1,100,480,coverage));
    for(x=0;x<100;++x){
        assert(coverage[x*480+100+2*x]==255);
        assert(coverage[x*480+102+2*x]==255);
        assert(!coverage[x*480+99+2*x]);
        assert(!coverage[x*480+103+2*x]);
    }
    /* Clip lines crossing the visible vertical range, including reversed gain. */
    for(i=0;i<256;++i)positions[i]=600-5.0*i;
    assert(waveform_view_connect_u8(data,2,1,positions,0,1,1,480,coverage));
    for(y=0;y<480;++y)assert(coverage[y]);
    assert(waveform_view_connect_u8(data,2,1,positions,4,1,1,480,coverage));
    for(y=0;y<480;++y)assert(!coverage[y]);
}

static void peak_detect_test(void)
{
    uint8_t raw[4096]={0},codes[4096],peaks[4096],dummy[4096]={0};
    CaptureBuffer in={0},out={0};
    CaptureDebug debug={1,{SIGNAL_WAVE_SQUARE,SIGNAL_WAVE_SINE},10000};
    CaptureView view={0,2048,{200,330},{100,100}};
    static DisplayFrame frame;
    unsigned i;
    in.data=raw;in.peaks=dummy;in.bytes=in.capacity=sizeof(raw);
    in.sample_rate_hz=1000;in.trigger_sample=CAPTURE_NO_TRIGGER;
    out.data=codes;out.peaks=peaks;out.capacity=sizeof(codes);
    assert(capture_debug_replace(&in,&out,&debug));
    for(i=0;i<2048;++i)assert(codes[2*i]>=127 && codes[2*i]<=129);
    for(i=0;i<sizeof(peaks);i+=4)assert(peaks[i]==127 && peaks[i+2]==189);
    view.skip_spectrum=1;
    assert(capture_process(&out,&view,&frame) && frame.peak_detect);
    assert(frame.peak_min[0][300]<=79 && frame.peak_max[0][300]>=201);
    assert(frame.minimum[0]<0 && frame.maximum[0]>1.2);
    view.display_first=1024;view.display_samples=64;
    assert(capture_process(&out,&view,&frame));
    unsigned low=INT16_MAX,high=0;
    for(i=0;i<SCOPE_PLOT_WIDTH;++i){
        if(frame.peak_min[0][i]<low)low=frame.peak_min[0][i];
        if(frame.peak_max[0][i]>high)high=frame.peak_max[0][i];
        /* Connected interpolation has no full-height isolated vertical marks. */
        assert(frame.y_max[0][i]-frame.y_min[0][i]<16);
    }
    assert(low<=80 && high>=200);
    /* The original extrema remain available when returning to the full view. */
    view.display_samples=0;
    assert(capture_process(&out,&view,&frame));
    assert(frame.peak_min[0][300]<=79 && frame.peak_max[0][300]>=201);
}

static void peak_zoom_continuity_test(void)
{
    static const unsigned spans[]={4096,2048,2000,1025,1024,1023,1000,512,128,64};
    uint8_t data[16384],peaks[16384];
    CaptureBuffer b={0};CaptureView v={0};static DisplayFrame f;
    unsigned s,i,x;
    memset(data,128,sizeof(data));
    for(i=0;i<sizeof(peaks);i+=4){peaks[i]=peaks[i+1]=100;peaks[i+2]=peaks[i+3]=160;}
    b.data=data;b.peaks=peaks;b.capacity=b.bytes=sizeof(data);
    b.sample_rate_hz=200000;b.zero_code[0]=b.zero_code[1]=128;
    b.volts_per_code[0]=b.volts_per_code[1]=0.02;b.trigger_sample=CAPTURE_NO_TRIGGER;
    v.zero_y[0]=v.zero_y[1]=200;v.pixels_per_volt[0]=v.pixels_per_volt[1]=100;v.skip_spectrum=1;
    for(s=0;s<sizeof(spans)/sizeof(spans[0]);++s){
        int low=INT16_MAX,high=INT16_MIN;
        v.display_samples=spans[s];v.display_first=4096-spans[s]/2.0;
        assert(capture_process(&b,&v,&f));
        assert(fabs(f.minimum[0]+0.56)<1e-12 && fabs(f.maximum[0]-0.64)<1e-12);
        for(x=0;x<SCOPE_PLOT_WIDTH;++x){
            if(f.peak_min[0][x]<low)low=f.peak_min[0][x];
            if(f.peak_max[0][x]>high)high=f.peak_max[0][x];
            assert(f.y[0][x]==200 && f.y_min[0][x]==200 && f.y_max[0][x]==200);
            assert(f.peak_min[0][x]==136 && f.peak_max[0][x]==256);
        }
        /* The native range is preserved independently of the sample trace. */
        assert(low==136 && high==256);
    }

    v.display_first=3596;v.display_samples=1000;v.density_enabled=1;
    assert(capture_process(&b,&v,&f));
    unsigned hits=0;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        if(f.density[0][x][200])++hits;
        assert(f.density[0][x][136]==0 && f.density[0][x][256]==0);
    }
    assert(hits==1000); /* Interpolated vertices never become invented density hits. */
    v.density_enabled=0;
    /* Small native noise excursions remain visible as a weak band, never
       promoted to a thick opaque trace or silently discarded. */
    for(i=0;i<sizeof(peaks);i+=4){peaks[i]=peaks[i+1]=127;peaks[i+2]=peaks[i+3]=129;}
    assert(capture_process(&b,&v,&f));
    assert(f.peak_min[0][300]==198 && f.peak_max[0][300]==202);
    assert(f.peak_strength[0][300]>0 && f.peak_strength[0][300]<255);
    /* A hidden pulse remains an interval, without a fabricated timed vertex. */
    memset(peaks,128,sizeof(peaks));peaks[4*2048+2]=255;
    for(s=2;s<sizeof(spans)/sizeof(spans[0]);++s){
        unsigned marks=0;
        v.display_samples=spans[s];v.display_first=4096-spans[s]/2.0;
        assert(capture_process(&b,&v,&f));
        for(x=0;x<SCOPE_PLOT_WIDTH;++x)if(f.peak_min[0][x]!=WAVEFORM_NO_SAMPLE){
            double left=v.display_first+x*v.display_samples/SCOPE_PLOT_WIDTH;
            double right=v.display_first+(x+1)*v.display_samples/SCOPE_PLOT_WIDTH;
            assert(left<4098 && right>4096);++marks;
        }
        assert(marks>=1 && marks<=(unsigned)ceil(2.0*SCOPE_PLOT_WIDTH/spans[s])+1);
    }
    /* Even with two hidden extrema, original sample direction is unchanged. */
    memset(data,128,sizeof(data));memset(peaks,128,sizeof(peaks));
    data[2048]=100;data[2050]=150;data[2052]=200;
    data[2049]=200;data[2051]=150;data[2053]=100;
    peaks[2048]=peaks[2049]=25;peaks[2050]=peaks[2051]=229;
    v.display_first=1024;v.display_samples=2;
    assert(capture_process(&b,&v,&f));
    assert(f.y[0][341]>f.y[0][683]);
    assert(f.peak_min[0][341]==-2 && f.peak_max[0][341]==406);
    assert(f.y[1][341]<f.y[1][683]);
    assert(f.peak_min[1][341]==-2 && f.peak_max[1][341]==406);
}

static void peak_resolved_trace_test(void)
{
    uint8_t data[4096],peaks[4096];
    CaptureBuffer b={0};CaptureView v={0};static DisplayFrame raw,peak;
    const double spans[]={4000,2049,2048,2047,1025,1024,1023,1000,64,4};
    unsigned kind,i,s,ch;
    b.data=data;b.capacity=b.bytes=sizeof(data);b.sample_rate_hz=200000;
    b.zero_code[0]=b.zero_code[1]=128;b.volts_per_code[0]=.02;b.volts_per_code[1]=-.02;
    b.trigger_sample=CAPTURE_NO_TRIGGER;
    v.zero_y[0]=v.zero_y[1]=200;v.pixels_per_volt[0]=v.pixels_per_volt[1]=100;v.skip_spectrum=1;
    for(kind=0;kind<5;++kind){
        for(i=0;i<2048;++i){
            unsigned code=kind==0?(unsigned)(128+60*sin(i*.07)):
                kind==1?60+i%128:kind==2?188-i%128:
                kind==3?(i%32<16?70:185):100+i*37%57;
            data[2*i]=data[2*i+1]=(uint8_t)code;
        }
        for(i=0;i<2048;i+=2)for(ch=0;ch<2;++ch){
            unsigned a=data[2*i+ch],c=data[2*(i+1)+ch];
            peaks[2*i+ch]=(uint8_t)(a<c?a:c);
            peaks[2*i+2+ch]=(uint8_t)(a>c?a:c);
        }
        for(s=0;s<sizeof(spans)/sizeof(spans[0]);++s){
            v.display_first=1024-spans[s]/2+.123;v.display_samples=spans[s];
            b.peaks=NULL;assert(capture_process(&b,&v,&raw));
            b.peaks=peaks;assert(capture_process(&b,&v,&peak));
            /* A resolved sine, rising/falling slope, square or noise retains
               exactly the original raster, including negative gain and gaps. */
            assert(!memcmp(raw.y,peak.y,sizeof(raw.y)));
            assert(!memcmp(raw.y_min,peak.y_min,sizeof(raw.y_min)));
            assert(!memcmp(raw.y_max,peak.y_max,sizeof(raw.y_max)));
            assert(!memcmp(raw.mean,peak.mean,sizeof(raw.mean)));
            assert(!memcmp(raw.rms,peak.rms,sizeof(raw.rms)));
            assert(!memcmp(raw.frequency_hz,peak.frequency_hz,sizeof(raw.frequency_hz)));
        }
    }
    /* Even extreme display interpolation cannot enter FFT, mean or RMS. */
    v.display_first=0;v.display_samples=2048;v.skip_spectrum=0;
    b.peaks=NULL;assert(capture_process(&b,&v,&raw));
    for(i=0;i<sizeof(peaks);i+=4){peaks[i]=peaks[i+1]=0;peaks[i+2]=peaks[i+3]=255;}
    b.peaks=peaks;assert(capture_process(&b,&v,&peak));
    assert(!memcmp(raw.fft,peak.fft,sizeof(raw.fft)));
    assert(!memcmp(raw.mean,peak.mean,sizeof(raw.mean)));
    assert(!memcmp(raw.rms,peak.rms,sizeof(raw.rms)));
    assert(!memcmp(raw.frequency_hz,peak.frequency_hz,sizeof(raw.frequency_hz)));
}

static unsigned sampled_sine_code(double sample, double period, double phase)
{
    return (unsigned)floor(128+60*sin(6.283185307179586*(sample/period+phase))+.5);
}

static void peak_sine_deviation_test(void)
{
    uint8_t data[4096],peaks[4096];
    CaptureBuffer b={0};CaptureView v={0};static DisplayFrame raw,peak;
    const unsigned periods[]={64,32,20,10,4};
    unsigned k,p,i,j,x;
    b.data=data;b.bytes=b.capacity=sizeof(data);b.sample_rate_hz=200000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;b.volts_per_code[0]=b.volts_per_code[1]=1;
    v.zero_y[0]=v.zero_y[1]=300;v.pixels_per_volt[0]=v.pixels_per_volt[1]=8;
    v.skip_spectrum=1;v.display_first=768;v.display_samples=128;
    for(k=0;k<sizeof(periods)/sizeof(periods[0]);++k)for(p=0;p<32;++p){
        for(i=0;i<2048;++i)
            data[2*i]=data[2*i+1]=(uint8_t)sampled_sine_code(i,periods[k],p/32.0);
        for(i=0;i<2048;i+=2){
            unsigned low=255,high=0;
            /* Peak Detect sees 32 native conversions per retained sample. */
            for(j=0;j<64;++j){
                unsigned c=sampled_sine_code(i+j/32.,periods[k],p/32.0);
                if(c<low)low=c;if(c>high)high=c;
            }
            peaks[2*i]=peaks[2*i+1]=(uint8_t)low;
            peaks[2*i+2]=peaks[2*i+3]=(uint8_t)high;
        }
        b.peaks=NULL;assert(capture_process(&b,&v,&raw));
        b.peaks=peaks;assert(capture_process(&b,&v,&peak));
        /* The displayed sample line is never replaced by invented extrema. */
        for(x=0;x<SCOPE_PLOT_WIDTH;++x)
            assert(peak.y[0][x]==raw.y[0][x]);
    }
}

int main(void)
{
    processor_density_test();
    connected_projection_test();
    peak_detect_test();
    peak_zoom_continuity_test();
    peak_resolved_trace_test();
    peak_sine_deviation_test();
    protocol_test();
    queue_test();
    ring_test();
    processor_file_test();
    processor_record_bounds_test();
    processor_short_pulse_test();
    processor_zoom_source_test();
    processor_fft_test();
    processor_statistics_test();
    processor_large_statistics_test();
    pipeline_lease_test();
    pipeline_test();
    capture_debug_test();
    noisy_edge_test();
    return 0;
}
