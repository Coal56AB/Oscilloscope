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

static void tiled_filter_test(void)
{
    const size_t maximum=1000000;
    uint8_t *codes=malloc(2*maximum);
    double *values=malloc(4*maximum*sizeof(double));assert(codes && values);
    double *expected=values+maximum,*temporary=expected+maximum,*actual=temporary+maximum;
    unsigned random=7;
    const size_t lengths[]={1,8,17,65,255,256,257,4097,maximum};
    for(unsigned format=0;format<2;++format)for(unsigned channel=0;channel<2;++channel){
        for(size_t i=0;i<maximum;++i){
            random=random*1664525u+1013904223u;
            codes[2*i+channel]=(uint8_t)((i%101<33?190:80)+(int)(random>>28)-8);
            values[i]=(codes[2*i+channel]-128.25)*.013;
        }
        if(format){values[255]=NAN;values[256]=NAN;values[4096]=NAN;}
        for(unsigned length=0;length<sizeof(lengths)/sizeof(lengths[0]);++length){
            size_t n=lengths[length];
            SignalTrace trace={codes+channel,values,n,2,.013,128.25,format};
            for(unsigned noise=0;noise<2;++noise)for(unsigned level=0;level<4;++level){
                double rms=noise?.035:0;
                if(format){
                    if(noise)signal_filter_denoise(values,n,rms,expected);
                    else memcpy(expected,values,n*sizeof(*expected));
                }else{
                    if(noise)signal_filter_denoise_u8(codes+channel,n,2,rms/.013,expected);
                    else for(size_t i=0;i<n;++i)expected[i]=codes[2*i+channel];
                    for(size_t i=0;i<n;++i)expected[i]=(expected[i]-128.25)*.013;
                }
                if(level){signal_filter_apply(expected,n,level,temporary);memcpy(expected,temporary,n*sizeof(*expected));}
                signal_filter_trace(&trace,rms,level,actual);
                for(size_t i=0;i<n;++i)
                    assert((isnan(actual[i]) && isnan(expected[i])) || fabs(actual[i]-expected[i])<1e-9);
            }
        }
    }
    free(values);free(codes);
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
    static DisplayFrame raw, filtered, no_peaks;
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
        double source[4096],expected[4096],sum=0,squares=0,low=INFINITY,high=-INFINITY;
        for(i=0;i<4096;++i)source[i]=(data[2*i]-128)*.01;
        signal_filter_apply(source,4096,level,expected);
        for(i=0;i<4096;++i){
            double value=expected[i];sum+=value;squares+=value*value;
            if(value<low)low=value;if(value>high)high=value;
        }
        assert(fabs(filtered.minimum[0]-low)<1e-12 && fabs(filtered.maximum[0]-high)<1e-12);
        assert(fabs(filtered.mean[0]-sum/4096)<1e-12 && fabs(filtered.rms[0]-sqrt(squares/4096))<1e-12);
        assert(filtered.maximum[0]-filtered.minimum[0]<raw.maximum[0]-raw.minimum[0]);
        assert(!memcmp(raw.raw_mean,filtered.raw_mean,sizeof(raw.raw_mean)));
        assert(!memcmp(raw.raw_rms,filtered.raw_rms,sizeof(raw.raw_rms)));
        assert(raw.minimum[1]==filtered.minimum[1] && raw.maximum[1]==filtered.maximum[1]);
        assert(raw.mean[1]==filtered.mean[1] && raw.rms[1]==filtered.rms[1]);

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
    assert(!memcmp(raw.fft,filtered.fft,sizeof(raw.fft)) && !memcmp(raw.raw_rms,filtered.raw_rms,sizeof(raw.raw_rms)));
    assert(filtered.rms[0]<raw.rms[0]);
    assert(!memcmp(raw.y[1],filtered.y[1],sizeof(raw.y[1])) && !memcmp(original,data,sizeof(data)));
    view.noise_rms[0]=0;view.filter_level[0]=3;
    assert(capture_process(&buffer,&view,&no_peaks));
    /* Unknown native extrema must not repaint noise over the filtered curve. */
    for (i = 0; i < sizeof(peaks); i += 4) {
        peaks[i] = peaks[i+1] = 0; peaks[i+2] = peaks[i+3] = 255;
    }
    buffer.peaks = peaks;
    assert(capture_process(&buffer, &view, &filtered));
    assert(!memcmp(no_peaks.minimum,filtered.minimum,sizeof(filtered.minimum[0])));
    assert(!memcmp(no_peaks.maximum,filtered.maximum,sizeof(filtered.maximum[0])));
    assert(!memcmp(no_peaks.mean,filtered.mean,sizeof(filtered.mean[0])));
    assert(!memcmp(no_peaks.rms,filtered.rms,sizeof(filtered.rms[0])));
    assert(!memcmp(no_peaks.raw_rms,filtered.raw_rms,sizeof(filtered.raw_rms)));
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

/* A fixed DC input must keep its coordinate through Time/div resampling,
   filtering and both raw/Peak Detect display paths, including density maps. */
static void dc_timebase_test(void)
{
    uint8_t data[16384],peaks[16384];
    CaptureBuffer buffer={0};CaptureView view={0};
    static DisplayFrame frame;
    const double steps[]={.05,.5,1,2,7.8125};
    buffer.data=data;buffer.bytes=buffer.capacity=sizeof(data);
    buffer.trigger_sample=CAPTURE_NO_TRIGGER;
    buffer.zero_code[0]=buffer.zero_code[1]=128.4;
    buffer.volts_per_code[0]=buffer.volts_per_code[1]=.1;
    for(unsigned i=0;i<8192;++i){data[2*i]=131;data[2*i+1]=121;}
    for(unsigned i=0;i<4096;++i){peaks[4*i]=peaks[4*i+2]=131;peaks[4*i+1]=peaks[4*i+3]=121;}
    view.display_first=17.25;view.skip_spectrum=1;
    view.zero_y[0]=240.25;view.zero_y[1]=300.75;
    view.pixels_per_volt[0]=17.5;view.pixels_per_volt[1]=23.5;
    view.density_enabled=1;view.density_rows=64;
    for(unsigned peak=0;peak<2;++peak){
        buffer.peaks=peak?peaks:NULL;
        buffer.sample_rate_hz=peak?100000000:200000000;
        for(unsigned level=0;level<4;++level)for(unsigned noise=0;noise<2;++noise){
            view.filter_level[0]=view.filter_level[1]=level;
            view.noise_rms[0]=view.noise_rms[1]=noise*.2;
            for(unsigned step=0;step<sizeof(steps)/sizeof(*steps);++step){
                view.display_samples=SCOPE_PLOT_WIDTH*steps[step];
                assert(capture_process(&buffer,&view,&frame));
                for(unsigned ch=0;ch<2;++ch){
                    double y=view.zero_y[ch]-(data[ch]-buffer.zero_code[ch])*.1*view.pixels_per_volt[ch];
                    unsigned row=(unsigned)(y*view.density_rows/SCOPE_PLOT_HEIGHT);
                    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x){
                        assert(frame.y[ch][x]==(int)y);
                        assert(frame.y_min[ch][x]==(int)y && frame.y_max[ch][x]==(int)y);
                        assert((&frame.connections[ch][0][0])[x*view.density_rows+row]);
                        for(unsigned r=0;r<view.density_rows;++r)if(r!=row)
                            assert(!frame.density[ch][x][r]);
                    }
                }
            }
        }
    }
}

static void adc_denoise_test(void)
{
    uint8_t data[4096*5],original[sizeof(data)];
    double input[4096],reference[4096],actual[4096];
    const unsigned lengths[]={0,1,2,8,16,17,33,4096};
    const unsigned strides[]={1,2,5};
    const double noise[]={0,.5,2,16,80};
    unsigned random=11;
    for(unsigned i=0;i<sizeof(data);++i){random=random*1664525u+1013904223u;data[i]=(uint8_t)(random>>24);}
    memcpy(original,data,sizeof(data));
    for(unsigned s=0;s<sizeof(strides)/sizeof(*strides);++s)
        for(unsigned n=0;n<sizeof(lengths)/sizeof(*lengths);++n)
            for(unsigned level=0;level<sizeof(noise)/sizeof(*noise);++level){
                unsigned stride=strides[s];
                for(unsigned i=0;i<lengths[n];++i)input[i]=data[i*stride];
                signal_filter_denoise(input,lengths[n],noise[level],reference);
                signal_filter_denoise_u8(data,lengths[n],stride,noise[level],actual);
                for(unsigned i=0;i<lengths[n];++i)assert(fabs(actual[i]-reference[i])<1e-10);
            }
    assert(!memcmp(data,original,sizeof(data)));
}

static void window_filter_test(void)
{
    enum { COUNT=16384 };
    uint8_t data[COUNT*2],original[COUNT*2];
    double input[COUNT],denoised[COUNT],expected[2][COUNT];
    CaptureBuffer buffer={0};CaptureView view={0},reference;
    static DisplayFrame actual,full;
    const double starts[]={-31.25,0,5000.25,COUNT-1023.75,COUNT+10};
    unsigned random=7;
    buffer.data=data;buffer.bytes=buffer.capacity=sizeof(data);buffer.sample_rate_hz=200000000;
    buffer.trigger_sample=CAPTURE_NO_TRIGGER;
    buffer.zero_code[0]=buffer.zero_code[1]=128;
    buffer.volts_per_code[0]=buffer.volts_per_code[1]=.007;
    for(unsigned i=0;i<COUNT*2;++i){
        random=random*1664525u+1013904223u;
        data[i]=(uint8_t)((i/2%307<153?80:170)+(random>>29));
    }
    memcpy(original,data,sizeof(data));
    view.skip_spectrum=1;view.density_enabled=1;view.density_rows=64;
    view.zero_y[0]=241.37;view.zero_y[1]=257.83;
    view.pixels_per_volt[0]=71.41;view.pixels_per_volt[1]=89.17;
    view.display_samples=1024;
    for(unsigned level=0;level<4;++level)for(unsigned noise=0;noise<2;++noise){
        if(!level && !noise)continue;
        view.filter_level[0]=view.filter_level[1]=level;
        view.noise_rms[0]=view.noise_rms[1]=noise*.03;
        for(unsigned ch=0;ch<2;++ch){
            for(unsigned i=0;i<COUNT;++i)input[i]=(data[i*2+ch]-128)*.007;
            signal_filter_denoise(input,COUNT,view.noise_rms[ch],denoised);
            signal_filter_apply(denoised,COUNT,level,expected[ch]);
        }
        for(unsigned pos=0;pos<sizeof(starts)/sizeof(*starts);++pos){
            view.display_first=starts[pos];reference=view;
            for(unsigned ch=0;ch<2;++ch){
                reference.filter_level[ch]=0;reference.noise_rms[ch]=0;
                reference.corrected[ch]=expected[ch];
            }
            assert(capture_process(&buffer,&reference,&full));
            assert(capture_process(&buffer,&view,&actual));
            assert(!memcmp(full.y,actual.y,sizeof(full.y)));
            assert(!memcmp(full.y_min,actual.y_min,sizeof(full.y_min)));
            assert(!memcmp(full.y_max,actual.y_max,sizeof(full.y_max)));
            assert(!memcmp(full.connections,actual.connections,sizeof(full.connections)));
            assert(!memcmp(full.density,actual.density,sizeof(full.density)));
            for(unsigned ch=0;ch<2;++ch){
                assert((isnan(full.mean[ch]) && isnan(actual.mean[ch])) || fabs(full.mean[ch]-actual.mean[ch])<1e-10);
                assert((isnan(full.rms[ch]) && isnan(actual.rms[ch])) || fabs(full.rms[ch]-actual.rms[ch])<1e-10);
                assert((isnan(full.minimum[ch]) && isnan(actual.minimum[ch])) || fabs(full.minimum[ch]-actual.minimum[ch])<1e-10);
                assert((isnan(full.maximum[ch]) && isnan(actual.maximum[ch])) || fabs(full.maximum[ch]-actual.maximum[ch])<1e-10);
            }
            assert(!memcmp(full.raw_mean,actual.raw_mean,sizeof(full.raw_mean)));
            assert(!memcmp(full.raw_rms,actual.raw_rms,sizeof(full.raw_rms)));
        }
    }
    assert(!memcmp(data,original,sizeof(data)));
}

static void growing_trigger_test(void)
{
    enum { COUNT=400000 };
    uint8_t *data=malloc(COUNT*2);
    double *input=malloc(COUNT*sizeof(*input)),*smooth=malloc(COUNT*sizeof(*smooth));
    const unsigned positions[]={1024,65400,65520,65536,131060,300000,COUNT-128};
    CaptureBuffer b={0};uint64_t sample;
    assert(data && input && smooth);
    b.data=data;b.bytes=b.capacity=COUNT*2;b.sample_rate_hz=200000000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;
    b.volts_per_code[0]=b.volts_per_code[1]=.004;b.zero_code[0]=b.zero_code[1]=128;
    for(unsigned test=0;test<=sizeof(positions)/sizeof(*positions);++test){
        memset(data,100,COUNT*2);
        if(test<sizeof(positions)/sizeof(*positions))
            for(unsigned i=positions[test];i<positions[test]+96;++i)data[2*i]=180;
        else for(unsigned i=30000;i<COUNT;++i)data[2*i]=(uint8_t)(100+(i-30000<100000?(i-30000)/1250:80));
        signal_filter_denoise_u8(data,COUNT,2,2,input);
        signal_filter_apply(input,COUNT,3,smooth);
        size_t expected=1;
        while(expected<COUNT && !(smooth[expected-1]<=128 && smooth[expected]>128))++expected;
        assert(expected<COUNT);
        assert(capture_find_trigger_filtered(&b,0,0,0,2,3,.008,0,&sample));
        assert(sample==expected);
        /* Already searched history must not be filtered again, but its source
           side still qualifies a later crossing with the same filter support. */
        uint64_t first=expected>200?expected-200:1;
        assert(capture_find_trigger_filtered(&b,0,0,0,2,3,.008,first,&sample));
        assert(sample==expected);
    }
    /* Long dwell inside the hysteresis band: recover the earlier low side. */
    memset(data,128,COUNT*2);
    for(unsigned i=0;i<1000;++i)data[2*i]=100;
    for(unsigned i=300000;i<COUNT;++i)data[2*i]=180;
    assert(capture_find_trigger_filtered(&b,0,0,0,2,3,.008,299900,&sample));
    assert(sample>=299960 && sample<=300040);
    memset(data,100,COUNT*2);
    assert(!capture_find_trigger_filtered(&b,0,0,0,2,3,.008,0,&sample));
    free(data);free(input);free(smooth);
}

static void denoise_bounds_test(void)
{
    enum { COUNT=512,R=SIGNAL_FILTER_DENOISE_RADIUS,N=R*2+1 };
    uint8_t codes[COUNT];double filtered[COUNT];
    const double noise[]={0,1,8,25,100};unsigned random=19;
    for(unsigned pattern=0;pattern<6;++pattern){
        for(unsigned i=0;i<COUNT;++i){
            random=random*1664525u+1013904223u;
            codes[i]=pattern==0?128:pattern==1?(i%2?100:180):pattern==2?(i==255?255:100):
                pattern==3?(i<256?40:220):pattern==4?(uint8_t)(random>>24):(uint8_t)(130+(random>>28));
        }
        double raw_low=255,raw_high=0,mean_low=255,mean_high=0,max_variance=0;
        for(unsigned i=R;i<COUNT-R;++i){
            unsigned sum=0,squares=0;
            for(unsigned j=i-R;j<=i+R;++j){sum+=codes[j];squares+=codes[j]*codes[j];}
            double mean=sum/(double)N,variance=(N*squares-sum*sum)/(double)(N*N);
            if(codes[i]<raw_low)raw_low=codes[i];if(codes[i]>raw_high)raw_high=codes[i];
            if(mean<mean_low)mean_low=mean;if(mean>mean_high)mean_high=mean;
            if(variance>max_variance)max_variance=variance;
        }
        for(unsigned k=0;k<sizeof(noise)/sizeof(*noise);++k){
            double low,high;
            signal_filter_denoise_u8(codes,COUNT,1,noise[k],filtered);
            signal_filter_denoise_bounds(raw_low,raw_high,mean_low,mean_high,max_variance,noise[k],&low,&high);
            for(unsigned i=R;i<COUNT-R;++i)assert(filtered[i]>=low-1e-9 && filtered[i]<=high+1e-9);
        }
    }
    CaptureBuffer metadata={0};metadata.volts_per_code[0]=.004;metadata.zero_code[0]=128;
    double low[2]={126.2,0},high[2]={180,0};
    assert(!capture_trigger_bounds_possible(&metadata,low,high,0,2,0,2));
    low[0]=125.99;
    assert(capture_trigger_bounds_possible(&metadata,low,high,0,2,0,2));
}

static void sparse_trigger_test(void)
{
    enum { COUNT=300000 };
    uint8_t *data=malloc(COUNT*2);
    CaptureBuffer b={0};uint64_t sample;
    const unsigned positions[]={64,255,256,257,8191,131071,COUNT-128};
    assert(data);
    b.data=data;b.bytes=b.capacity=COUNT*2;b.sample_rate_hz=50000000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;b.first_sample=123456;
    b.volts_per_code[0]=b.volts_per_code[1]=.08;b.zero_code[0]=b.zero_code[1]=128;
    for(unsigned ch=0;ch<2;++ch)for(unsigned filter=0;filter<4;++filter){
        /* Idle noise includes isolated raw crossings of the guard. Denoising
           removes them; no packet is present anywhere in the record. */
        for(unsigned i=0;i<COUNT;++i){data[2*i+ch]=(uint8_t)(170+(i%7)-3);data[2*i+1-ch]=128;}
        for(unsigned i=255;i<COUNT;i+=16383)data[2*i+ch]=145;
        assert(!capture_find_trigger_filtered(&b,ch,1,1.76,2,filter,.5,b.first_sample,&sample));
        for(unsigned edge=0;edge<2;++edge)for(unsigned p=0;p<sizeof(positions)/sizeof(*positions);++p){
            unsigned at=positions[p];
            for(unsigned i=0;i<COUNT;++i)data[2*i+ch]=(uint8_t)(edge?180:100);
            for(unsigned i=at;i<at+96;++i)data[2*i+ch]=(uint8_t)(edge?100:180);
            assert(capture_find_trigger_filtered(&b,ch,edge,0,2,filter,.16,b.first_sample,&sample));
            assert(sample>=b.first_sample+(at>40?at-40:1) && sample<=b.first_sample+at+40);
            assert(capture_find_trigger_filtered(&b,ch,2,0,2,filter,.16,b.first_sample,&sample));
            assert(sample>=b.first_sample+(at>40?at-40:1) && sample<=b.first_sample+at+40);
        }
    }
    /* Confirmation can arrive far after the level crossing. The cheap range
       scan must not replace that crossing with its later tile anchor. */
    memset(data,128,COUNT*2);
    for(unsigned i=0;i<1000;++i)data[2*i]=100;
    for(unsigned i=1000;i<150000;++i)data[2*i]=127;
    for(unsigned i=150000;i<COUNT-1000;++i)data[2*i]=129;
    for(unsigned i=COUNT-1000;i<COUNT;++i)data[2*i]=180;
    assert(capture_find_trigger_filtered(&b,0,0,0,2,3,.16,b.first_sample+140000,&sample));
    assert(sample>=b.first_sample+149960 && sample<=b.first_sample+150040);
    memset(data,128,COUNT*2);
    assert(!capture_find_trigger_filtered(&b,0,2,0,2,3,.16,b.first_sample+100000,&sample));
    /* The preceding burst remains in history after its last falling
       edge. A completed search must still find the next packet, not the old
       crossing or a filter boundary in the intervening idle level. */
    for(unsigned i=0;i<COUNT;++i)data[2*i]=(uint8_t)(i>=1000 && i<2000?100:180);
    assert(!capture_find_trigger_filtered(&b,0,1,0,2,2,.16,b.first_sample+1100,&sample));
    for(unsigned i=200000;i<201000;++i)data[2*i]=100;
    assert(capture_find_trigger_filtered(&b,0,1,0,2,2,.16,b.first_sample+1100,&sample));
    assert(sample>=b.first_sample+199960 && sample<=b.first_sample+200040);
    free(data);
}

static void hidden_density_test(void)
{
    uint8_t data[8192];CaptureBuffer b={0};CaptureView v={0};
    static DisplayFrame full,hidden;
    b.data=data;b.bytes=b.capacity=sizeof(data);b.sample_rate_hz=200000000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;
    b.volts_per_code[0]=b.volts_per_code[1]=.004;b.zero_code[0]=b.zero_code[1]=128;
    for(unsigned i=0;i<sizeof(data);++i)data[i]=(uint8_t)(i%73+80);
    v.samples=4096;v.density_enabled=1;v.density_rows=64;v.skip_spectrum=1;
    v.zero_y[0]=v.zero_y[1]=240;v.pixels_per_volt[0]=v.pixels_per_volt[1]=200;
    assert(capture_process(&b,&v,&full));
    v.hidden_channel_mask=2;assert(capture_process(&b,&v,&hidden));
    assert(!memcmp(full.y[0],hidden.y[0],sizeof(full.y[0])));
    for(size_t x=0;x<SCOPE_PLOT_WIDTH;++x)assert(hidden.y[1][x]==WAVEFORM_NO_SAMPLE);
    assert(!memcmp(full.mean,hidden.mean,sizeof(full.mean)));
    assert(!memcmp(full.rms,hidden.rms,sizeof(full.rms)));
    assert(!memcmp(full.minimum,hidden.minimum,sizeof(full.minimum)));
    assert(!memcmp(full.maximum,hidden.maximum,sizeof(full.maximum)));
    assert(!memcmp(full.density[0],hidden.density[0],sizeof(full.density[0])));
    assert(!memcmp(full.connections[0],hidden.connections[0],sizeof(full.connections[0])));
    for(size_t i=0;i<sizeof(hidden.density[1]);++i){
        assert(!(&hidden.density[1][0][0])[i]);assert(!(&hidden.connections[1][0][0])[i]);
    }
    v.hidden_channel_mask=0;assert(capture_process(&b,&v,&hidden));
    assert(!memcmp(full.density,hidden.density,sizeof(full.density)));
    assert(!memcmp(full.connections,hidden.connections,sizeof(full.connections)));
    assert(!memcmp(full.y,hidden.y,sizeof(full.y)));
    v.hidden_channel_mask=2;assert(capture_process(&b,&v,&hidden));
    for(size_t i=0;i<sizeof(hidden.density[1]);++i){
        assert(!(&hidden.density[1][0][0])[i]);assert(!(&hidden.connections[1][0][0])[i]);
    }
}

static void timing_measurement_test(void)
{
    enum { COUNT=40000 };
    uint8_t *data=malloc(COUNT*2);CaptureBuffer b={0};CaptureView v={0};static DisplayFrame f;
    const double frequencies[]={11000,18000,1000000};
    const double duties[]={.01,.25,.5,.75,.99};
    assert(data);
    b.data=data;b.bytes=b.capacity=COUNT*2;b.sample_rate_hz=200000000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;
    b.volts_per_code[0]=b.volts_per_code[1]=.004;b.zero_code[0]=b.zero_code[1]=128;
    v.samples=COUNT;v.skip_spectrum=1;v.hidden_channel_mask=2;
    v.filter_level[0]=2;v.noise_rms[0]=.12;v.zero_y[0]=240;v.pixels_per_volt[0]=100;
    for(unsigned k=0;k<3;++k)for(unsigned d=0;d<5;++d){
        if(k==2 && (d==0 || d==4))continue; /* Pulses shorter than the chosen filter support. */
        for(unsigned phase=0;phase<8;++phase){
            unsigned random=phase+31;
            for(unsigned i=0;i<COUNT;++i){
                double t=i+phase*3127.3,cycle=t*frequencies[k]/b.sample_rate_hz;
                random=random*1664525u+1013904223u;
                double noise=18*sin(6.283185307179586*.05*t)+(int)(random>>29)-3;
                data[2*i]=(uint8_t)lround(135+(cycle-floor(cycle)<duties[d]?15:-15)+noise);
                data[2*i+1]=128;
            }
            assert(capture_process(&b,&v,&f));
            assert(fabs(f.frequency_hz[0]/frequencies[k]-1)<.003);
            assert(fabs(f.duty[0]-100*duties[d])<.5);
        }
    }
    v.filter_level[0]=0;v.noise_rms[0]=0;
    for(unsigned i=0;i<COUNT;++i)data[2*i]=i%2?210:40;
    assert(capture_process(&b,&v,&f));
    assert(f.frequency_hz[0]==100000000 && f.duty[0]==50);
    memset(data,128,COUNT*2);
    assert(capture_process(&b,&v,&f));assert(f.frequency_hz[0]==0 && isnan(f.duty[0]));
    v.samples=1000;
    for(unsigned i=0;i<COUNT;++i)data[2*i]=i<COUNT/2?100:180;
    assert(capture_process(&b,&v,&f));assert(f.frequency_hz[0]==0 && isnan(f.duty[0]));
    free(data);
}

static void automatic_peak_voltage_test(void)
{
    uint8_t data[8192],peaks[8192];CaptureBuffer b={0};CaptureView v={0};
    static DisplayFrame f;
    b.data=data;b.peaks=peaks;b.bytes=b.capacity=sizeof(data);b.sample_rate_hz=100000000;
    b.trigger_sample=CAPTURE_NO_TRIGGER;
    b.volts_per_code[0]=b.volts_per_code[1]=.01;b.zero_code[0]=b.zero_code[1]=128;
    for(unsigned i=0;i<4096;++i){
        unsigned code=i%1024<512?80:176;
        data[2*i]=data[2*i+1]=(uint8_t)code;
        if(!(i%2))for(unsigned ch=0;ch<2;++ch){
            peaks[2*i+ch]=(uint8_t)(code-10);peaks[2*i+2+ch]=(uint8_t)(code+10);
        }
    }
    v.samples=4096;v.skip_spectrum=1;v.noise_rms[0]=.03;v.hidden_channel_mask=2;
    v.zero_y[0]=240;v.pixels_per_volt[0]=200;
    assert(capture_process(&b,&v,&f));
    /* Each native interval has only noise, although the whole window spans a
       square wave. Aggregation must not restore suppressed native excursions. */
    assert(fabs(f.minimum[0]+.48)<1e-12 && fabs(f.maximum[0]-.48)<1e-12);
    for(unsigned x=0;x<SCOPE_PLOT_WIDTH;++x)assert(!f.peak_strength[0][x]);
}

int main(void)
{
    tiled_filter_test();
    kernel_test(); denoise_test(); adc_denoise_test();denoise_bounds_test();processor_test(); stopped_pipeline_test();dc_timebase_test();window_filter_test();growing_trigger_test();sparse_trigger_test();hidden_density_test();timing_measurement_test();automatic_peak_voltage_test();
    return 0;
}
