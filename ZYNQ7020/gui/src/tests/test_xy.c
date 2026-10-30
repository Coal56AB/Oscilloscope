#ifdef NDEBUG
#undef NDEBUG
#endif
#include "capture_adapter.h"
#include "demo_settings.h"
#include "settings_session.h"
#include "scope_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DemoSignal demo;
static DisplayFrame frame;
static uint32_t pixels[SCOPE_WIDTH*SCOPE_HEIGHT];

static void raw_pairs(void)
{
    enum { COUNT=20000 };
    static uint8_t data[COUNT*2],peaks[COUNT*2];
    CaptureBuffer buffer={0};CaptureView view={0};
    for(unsigned i=0;i<COUNT;++i){data[2*i]=(uint8_t)(40+i%170);data[2*i+1]=data[2*i];}
    memset(peaks,255,sizeof(peaks));
    buffer.data=data;buffer.bytes=buffer.capacity=sizeof(data);buffer.sample_rate_hz=200000000;
    buffer.volts_per_code[0]=buffer.volts_per_code[1]=.01;
    buffer.zero_code[0]=buffer.zero_code[1]=128;buffer.trigger_sample=CAPTURE_NO_TRIGGER;
    view.start=13;view.samples=COUNT-29;view.xy_enabled=1;view.skip_spectrum=1;
    view.zero_y[0]=200;view.zero_y[1]=330;
    view.pixels_per_volt[0]=view.pixels_per_volt[1]=60;
    assert(capture_process(&buffer,&view,&frame));
    assert(frame.xy_count==WAVEFORM_XY_POINTS);
    for(unsigned i=0;i<frame.xy_count;++i)assert(frame.xy[1][i]-frame.xy[0][i]==130);
    int16_t original[2][WAVEFORM_XY_POINTS];memcpy(original,frame.xy,sizeof(original));
    /* Peak envelopes deliberately disagree with the acquired sample pairs. */
    buffer.peaks=peaks;assert(capture_process(&buffer,&view,&frame));
    assert(frame.peak_detect && !memcmp(original,frame.xy,sizeof(original)));
    view.filter_level[0]=view.filter_level[1]=2;
    assert(capture_process(&buffer,&view,&frame));
    for(unsigned i=0;i<frame.xy_count;++i)assert(frame.xy[1][i]-frame.xy[0][i]==130);
    view.filter_level[0]=0;view.filter_level[1]=3;view.display_first=-20;view.display_samples=100;
    assert(capture_process(&buffer,&view,&frame) && frame.xy_count==80);
    view.display_first=COUNT+10;assert(capture_process(&buffer,&view,&frame) && !frame.xy_count);
    view.hidden_channel_mask=1;view.display_first=0;
    assert(capture_process(&buffer,&view,&frame) && !frame.xy_count);
}

static void transitions(void)
{
    DemoSettings saved,loaded;
    demo_signal_init(&demo);
    demo_signal_ui_toggle_zoom(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_move_cursor(&demo,310);
    int cursor=demo.screen.cursor_a,zoom=demo.zoom_time_index;
    int positions[2]={demo.channel_position[0],demo.channel_position[1]};
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    assert(demo.screen.xy_count && demo.screen.zoom_enabled && demo.zoom_time_index==zoom);
    assert(scope_screen_waveform_height(&demo.screen)==SCOPE_PLOT_HEIGHT);
    assert(scope_screen_waveform_top(&demo.screen)==SCOPE_PLOT_Y);
    assert(!demo.screen.cursor_measurement_count && !demo.screen.fft_cursor_visible);
    assert(!demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,DEMO_PROCESS_FFT,1));
    demo_signal_ui_cycle_cursor_mode(&demo);demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,4);
    assert(demo.screen.cursor_a==cursor && demo.screen.cursor_mode==SCOPE_CURSOR_TIME);
    demo_signal_move_channel(&demo,0,70);demo_signal_move_channel(&demo,1,-45);
    assert(demo.screen.xy_position[0]==70 && demo.screen.xy_position[1]==-45);
    assert(demo.channel_position[0]==positions[0] && demo.channel_position[1]==positions[1]);
    demo.position_mode[0]=1;demo_signal_rotate(&demo,DEMO_ENC_CH1,1);
    assert(demo.screen.xy_position[0]==78);
    demo_signal_ui_toggle_run(&demo);uint32_t sequence=demo.capture_sequence;
    int16_t frozen[2][WAVEFORM_XY_POINTS];memcpy(frozen,demo.xy,sizeof(frozen));
    demo_signal_advance(&demo);
    assert(!demo.screen.running && sequence==demo.capture_sequence && !memcmp(frozen,demo.xy,sizeof(frozen)));
    unsigned count=demo.screen.xy_count;
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    assert(demo.screen.xy_count==count && !memcmp(frozen,demo.xy,sizeof(frozen)));
    assert(demo_signal_set_option(&demo,DEMO_MENU_CH1,DEMO_CHANNEL_FILTER,2));
    assert(demo_signal_set_option(&demo,DEMO_MENU_CH2,DEMO_CHANNEL_FILTER,1));
    assert(demo.filter_level[0]==2 && demo.filter_level[1]==1);
    CaptureRequest request;capture_request_from_demo(&demo,&request);
    assert(request.xy_enabled && !request.spectrum_enabled && !request.density_enabled);
    assert(request.duration_seconds==request.overview_duration_seconds);
    demo.baseline_disabled[2]=CHANNEL_NOISE_VALID;
    demo_settings_capture(&demo,&saved);assert(demo_settings_valid(&saved));
    demo_signal_init(&demo);assert(demo_settings_apply(&demo,&saved));
    demo_settings_capture(&demo,&loaded);assert(!memcmp(&saved,&loaded,sizeof(saved)));
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,0));
    assert(demo.screen.zoom_enabled && demo.zoom_time_index==zoom && demo.screen.cursor_a==cursor);
    assert(demo.screen.cursor_measurement_count && !demo.screen.running);
    demo_signal_ui_toggle_zoom(&demo);
    assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,DEMO_PROCESS_FFT,1));
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    assert(demo.fft_enabled && !demo.screen.fft_cursor_visible);
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,0));
    assert(demo.fft_enabled && demo.screen.fft_cursor_visible);
    /* Acquisition controls remain available across rate/depth/ROLL changes. */
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    for(int depth=0;depth<5;++depth){
        assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,DEMO_PROCESS_RECORD,depth));
        assert(demo_signal_set_option(&demo,DEMO_MENU_TIME,0,depth?26:0));
        capture_request_from_demo(&demo,&request);
        assert(request.record_samples && request.xy_enabled);
    }
    demo_signal_ui_toggle_run(&demo);assert(demo.screen.running);
    demo_signal_init(&demo);demo_signal_ui_toggle_zoom(&demo);
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    assert(demo_signal_set_option(&demo,DEMO_MENU_TIME,0,0));
    demo_settings_capture(&demo,&saved);assert(demo_settings_valid(&saved));
    assert(demo.zoom_time_index==demo.time_index);
    demo_signal_init(&demo);demo.trigger_mode_index=1;demo.trigger_voltage=100;
    demo_signal_refresh(&demo);assert(demo.screen.waiting_for_trigger);
    assert(demo_signal_set_option(&demo,DEMO_MENU_DISPLAY,DEMO_DISPLAY_MODE,1));
    assert(demo.screen.waiting_for_trigger && demo.screen.xy_count);
}

static void rendering(void)
{
    int16_t x[]={140,240,340,WAVEFORM_NO_SAMPLE,INT16_MAX};
    int16_t y[]={140,240,340,140,140};
    ScopeScreen screen={0};
    screen.xy_mode=1;screen.ch1_enabled=screen.ch2_enabled=1;
    screen.xy_samples[0]=x;screen.xy_samples[1]=y;screen.xy_count=5;
    screen.channel_zero_y[0]=screen.channel_zero_y[1]=240;
    screen.plot_only=1;screen.grid_brightness=5;
    scope_screen_render(pixels,SCOPE_WIDTH,&screen);
    assert(pixels[(SCOPE_PLOT_Y+240)*SCOPE_WIDTH+512]==0x70dfa3);
    assert(pixels[(SCOPE_PLOT_Y+140)*SCOPE_WIDTH+683]==0x70dfa3);
    assert(pixels[(SCOPE_PLOT_Y+340)*SCOPE_WIDTH+341]==0x70dfa3);
    /* Neither an unpaired sample nor an offscreen value creates a rail. */
    assert(pixels[(SCOPE_PLOT_Y+140)*SCOPE_WIDTH+1]!=0x70dfa3);
    screen.xy_position[0]=30;screen.xy_position[1]=-20;
    scope_screen_render(pixels,SCOPE_WIDTH,&screen);
    assert(pixels[(SCOPE_PLOT_Y+220)*SCOPE_WIDTH+542]==0x70dfa3);
    screen.ch1_enabled=0;scope_screen_render(pixels,SCOPE_WIDTH,&screen);
    assert(pixels[(SCOPE_PLOT_Y+220)*SCOPE_WIDTH+542]!=0x70dfa3);
}

static void calibration_application(void)
{
    demo_signal_init(&demo);
    for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage){
        ChannelBaseline baseline={3,.1,.02,.02,.02};
        demo.baseline[2][stage]=baseline;
    }
    ChannelBaseline original[INPUT_RANGE_STAGE_COUNT];memcpy(original,demo.baseline[2],sizeof(original));
    assert(demo_signal_set_option(&demo,DEMO_MENU_ZERO,1,0));
    ChannelBaseline active=demo_signal_applied_baseline(&demo,0,0);
    assert(channel_baseline_zero(&active,.02)==0 && channel_baseline_noise(&active,.02)==.02);
    assert(demo_signal_set_option(&demo,DEMO_MENU_NOISE,1,0));
    active=demo_signal_applied_baseline(&demo,0,0);assert(!active.valid);
    CaptureRequest request;capture_request_from_demo(&demo,&request);
    for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage)assert(!request.baseline[0][stage].valid);
    SettingsDocument document={0},decoded={0};char data[SETTINGS_STORE_BYTES];
    demo_settings_capture(&demo,&document.last);memcpy(document.baseline,demo.baseline,sizeof(document.baseline));
    assert(settings_document_encode(&document,data,sizeof(data)) && settings_document_decode(&decoded,data));
    demo_signal_init(&demo);memcpy(demo.baseline,decoded.baseline,sizeof(demo.baseline));
    assert(demo_settings_apply(&demo,&decoded.last));assert(demo.baseline_disabled[2]==3);
    assert(demo_signal_set_option(&demo,DEMO_MENU_ZERO,1,1));
    assert(demo_signal_set_option(&demo,DEMO_MENU_NOISE,1,1));
    assert(!memcmp(original,demo.baseline[2],sizeof(original)));
    active=demo_signal_applied_baseline(&demo,0,0);
    assert(channel_baseline_zero(&active,.02)==.1 && channel_baseline_noise(&active,.02)==.02);
}

static void snapshot_draw(uint32_t *output,int stride,const ScopeScreen *screen)
{
    (void)stride;output[0]=(uint32_t)screen->xy_samples[0][0];output[1]=(uint32_t)screen->xy_samples[1][0];
}

static void renderer_snapshot(void)
{
    int16_t values[2]={123,456};ScopeScreen screen={0};
    screen.xy_samples[0]=values;screen.xy_samples[1]=values+1;screen.xy_count=1;
    ScopeRenderer *renderer=scope_renderer_create(sizeof(values),snapshot_draw);assert(renderer);
    ScopeRenderRegion region={values,sizeof(values)};
    uint64_t generation=scope_renderer_submit(renderer,&screen,&region,1);assert(generation);
    values[0]=values[1]=0;
    assert(scope_renderer_flush(renderer,generation,pixels,5000));
    assert(pixels[0]==123 && pixels[1]==456);scope_renderer_destroy(renderer);
}

int main(void)
{
    raw_pairs();transitions();rendering();calibration_application();renderer_snapshot();
    puts("XY sample pairing, view transitions, calibration application and projection passed");return 0;
}
