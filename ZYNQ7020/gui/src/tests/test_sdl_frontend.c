#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#define main scope_preview_main
#include "../preview_sdl.c"
#undef main
#include <assert.h>
#include <fcntl.h>

#ifdef SCOPE_ENABLE_ZYNQ
static int startup_testing,startup_storage_calls,startup_adc_calls,startup_capture_calls;
static unsigned startup_ticks,startup_cancel_at;
static int startup_cancel_event;
extern Uint32 __real_SDL_GetTicks(void);
extern void __real_scope_sleep_ms(unsigned milliseconds);
extern int __real_linux_storage_check(const char *path,int require_device);
Uint32 __wrap_SDL_GetTicks(void)
{
    return startup_testing?startup_ticks:__real_SDL_GetTicks();
}
void __wrap_scope_sleep_ms(unsigned milliseconds)
{
    if(!startup_testing){__real_scope_sleep_ms(milliseconds);return;}
    assert(!capture_pipeline && !demo.external_capture);
    startup_ticks+=milliseconds;
    if(startup_cancel_at && startup_ticks>=startup_cancel_at){
        if(startup_cancel_event){SDL_Event event={.type=SDL_QUIT};assert(SDL_PushEvent(&event)==1);}
        else stop_requested=1;
    } else if(!startup_cancel_at && startup_ticks>=7000)serial_controls.metrics.connected=1;
}
int __wrap_linux_storage_check(const char *path,int require_device)
{
    if(!startup_testing)return __real_linux_storage_check(path,require_device);
    assert(!capture_pipeline && !demo.external_capture && require_device);
    return ++startup_storage_calls>1;
}
int __wrap_capture_selftest(const char *device,CaptureSelftestProgress progress,void *context,
                           char *error,size_t error_size)
{
    assert(startup_testing && !capture_pipeline && !demo.external_capture);
    assert(!strcmp(device,"/dev/test-startup"));
    ++startup_adc_calls;
    if(progress)progress(1562500,0,context);
    if(startup_adc_calls==1){snprintf(error,error_size,"ADC TIMEOUT");return 0;}
    return 1;
}
CapturePipeline *__wrap_capture_pipeline_create_zynq(const char *device,const double gain[2],const double zero[2])
{
    assert(startup_testing && !strcmp(device,"/dev/test-startup"));
    assert(!capture_pipeline && !demo.external_capture && gain[0]>0 && zero[0]==128);
    return ++startup_capture_calls>1?(CapturePipeline *)(uintptr_t)1:NULL;
}
static void test_startup_retry(void)
{
    StartupView view={.status_fd=-1};
    ControlEvent event={0};
    unsigned ticks_after;
    assert(SDL_Init(SDL_INIT_TIMER|SDL_INIT_EVENTS)==0);
    control_queue_init(&controls);
    assert(control_queue_push(&controls,&event));
    demo_signal_init(&demo);capture_pipeline=NULL;stop_requested=0;
    memset(&serial_controls,0,sizeof(serial_controls));
    require_data_device=1;startup_ticks=0;startup_testing=1;
    /* USB appears after the first six-second check. SD, ADC and acquisition
       creation then each fail once before the same process starts the GUI. */
    assert(startup_wait_ready(&view,"/dev/test-startup",1));
    assert(startup_storage_calls==4 && startup_adc_calls==3 && startup_capture_calls==2);
    assert(capture_pipeline && demo.external_capture && !*startup_error);
    assert(!control_queue_pop(&controls,&event));
    ticks_after=startup_ticks;
    capture_pipeline=NULL;demo_signal_init(&demo);serial_controls.metrics.connected=0;
    startup_cancel_at=ticks_after+200;startup_cancel_event=0;
    assert(!startup_wait_ready(&view,"/dev/test-startup",1) && stop_requested);
    assert(!capture_pipeline && startup_capture_calls==2);
    /* Closing the window also cancels an indefinite startup wait. */
    stop_requested=0;startup_cancel_at=startup_ticks+200;startup_cancel_event=1;
    assert(!startup_wait_ready(&view,"/dev/test-startup",1) && stop_requested);
    assert(!capture_pipeline && startup_capture_calls==2);
    startup_testing=startup_cancel_at=startup_cancel_event=stop_requested=require_data_device=0;
    memset(&serial_controls,0,sizeof(serial_controls));
    control_queue_destroy(&controls);SDL_Quit();
}
#endif

static void test_debug_hold_threshold(void)
{
    HeldControl held = {0};
    demo_signal_init(&demo);
    assert(demo_signal_set_option(&demo, DEMO_MENU_DEBUG, 1, 12)); /* 800 ms */
    begin_press(&held, DEMO_BTN_CURSOR, 0, 1000);
    fire_long_press(&held, 1799);
    assert(!held.long_done && !demo.screen.menu_open);
    fire_long_press(&held, 1800);
    assert(held.long_done && demo.menu_kind == DEMO_MENU_CURSOR);
    finish_press(&held, 1801);
    assert(!held.active && demo.screen.cursor_mode == SCOPE_CURSOR_OFF);
    demo_signal_ui_dismiss_menu(&demo);
    begin_press(&held, DEMO_BTN_CURSOR, 0, 2000);
    finish_press(&held, 2799);
    assert(!demo.screen.menu_open && demo.screen.cursor_mode == SCOPE_CURSOR_TIME);
    begin_press(&held, DEMO_BTN_CURSOR, 0, 3000);
    finish_press(&held, 3800);
    assert(demo.menu_kind == DEMO_MENU_CURSOR && demo.screen.cursor_mode == SCOPE_CURSOR_TIME);
}

static void test_restored_settings(void)
{
    ControlEvent event={.type=CONTROL_SCALE,.control=0,.value=1000};
    demo_signal_init(&demo);
    demo.scale_index[0]=1;
    restored_scale[0]=200;restored_scale[1]=-1;
    control_queue_init(&controls);
    assert(control_queue_push(&controls,&event));
    drain_controls(100);
    assert(demo.scale_index[0]==1 && restored_scale[0]==200);
    event.value=200;assert(control_queue_push(&controls,&event));
    drain_controls(101);
    assert(demo.scale_index[0]==1 && restored_scale[0]==-1);
    event.value=500;assert(control_queue_push(&controls,&event));
    drain_controls(102);
    assert(demo.scale_index[0]==2);
    control_queue_destroy(&controls);
    live_before_browse=demo;live_snapshot_valid=1;
    demo.startup_preset=3;demo.preset_valid=7;demo.scale_index[0]=5;
    assert(live_settings()->scale_index[0]==2);
    assert(live_settings()->startup_preset==3 && live_settings()->preset_valid==7);
    act(DEMO_ACTION_BROWSE_CLOSE);
    assert(!live_snapshot_valid && demo.scale_index[0]==2 && demo.startup_preset==3);
}

static void test_response_capture(void)
{
    static DisplayFrame f;
    CaptureRequest request;FILE *reply=tmpfile();assert(reply);
    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    assert(remote_command("calibrate 1 start",reply,NULL));
    capture_request_from_demo(&demo,&request);
    assert(demo.response_active && request.running && request.spectrum_enabled);
    assert(request.debug.wave[0]==SIGNAL_WAVE_SINE && request.debug.frequency_hz==100);
    assert(!request.response_enabled[0]);
    f.sequence=1;f.sample_rate_hz=65536;f.fft_size=1024;f.calibration_mask=1;
    f.fundamental_hz[0]=1000;f.fundamental_amplitude[0]=1;f.rms[0]=sqrt(.5);
    capture_display_apply(&demo,&f);
    f.sequence=2;f.fundamental_hz[0]=10000;f.fundamental_amplitude[0]=.5;f.rms[0]=sqrt(.125);
    capture_display_apply(&demo,&f);
    assert(demo.response[2].count==2 && demo.response[0].count==0);
    f.sequence=3;f.fundamental_hz[0]=3000;f.clipped_mask=1;
    capture_display_apply(&demo,&f);assert(demo.response[2].count==2);
    f.sequence=4;f.clipped_mask=0;f.rms[0]=10;
    capture_display_apply(&demo,&f);assert(demo.response[2].count==2);
    assert(remote_set("ch1_response",1));
    capture_request_from_demo(&demo,&request);assert(!request.response_enabled[0]);
    assert(remote_command("calibrate 1 stop",reply,NULL));
    assert(remote_command("calibrate 1 exit",reply,NULL));
    capture_request_from_demo(&demo,&request);assert(request.response_enabled[0]);
    assert(remote_set("simulation",0));assert(!demo.response_enabled[0]);
    assert(!demo.response[0].count && demo.response[2].count==2);
    pc_activity_until=0;
    fclose(reply);
}

static void test_external_trigger_marker(void)
{
    static DisplayFrame f={0};
    CaptureRequest r;
    demo_signal_init(&demo);
    demo_signal_begin_external(&demo);
    f.sample_rate_hz=200000000;f.trigger_sample=100;f.trigger_x=320;
    capture_display_apply(&demo,&f);
    assert(demo.screen.trigger_locked&&demo.screen.trigger_marker_x==320);
    f.trigger_x=-20;capture_display_apply(&demo,&f);
    assert(demo.screen.trigger_marker_x==0);
    f.trigger_x=2000;capture_display_apply(&demo,&f);
    assert(demo.screen.trigger_marker_x==SCOPE_PLOT_WIDTH-1);
    demo.screen.zoom_enabled=1;f.trigger_x=400;f.overview_start=200;f.overview_end=700;
    capture_display_apply(&demo,&f);
    assert(demo.screen.trigger_marker_x==400&&demo.screen.zoom_window_start==200&&demo.screen.zoom_window_end==700);
    demo.fft_enabled=0;capture_request_from_demo(&demo,&r);assert(!r.spectrum_enabled);
    demo.fft_enabled=1;capture_request_from_demo(&demo,&r);assert(r.spectrum_enabled);
    assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,0,2));
    assert(demo_signal_set_option(&demo,DEMO_MENU_PROCESSING,1,3));
    capture_request_from_demo(&demo,&r);
    assert(r.filter_level[0]==2 && r.filter_level[1]==3);
}

static void test_adc_vertical_ranges(void)
{
    static const double lower[6]={962210,962210,52210,52210,52210,5210};
    uint8_t codes[8]={118,123,138,133,118,123,138,133};
    CaptureBuffer buffer={0};
    CaptureRequest request;
    CaptureView view={0};
    static DisplayFrame frame;
    double span[6], voltage[6];
    unsigned i,ch;
    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    buffer.data=codes;buffer.bytes=buffer.capacity=sizeof(codes);
    buffer.sample_rate_hz=1000;buffer.trigger_sample=CAPTURE_NO_TRIGGER;
    buffer.zero_code[0]=buffer.zero_code[1]=128;
    view.samples=sizeof(codes)/2;view.skip_spectrum=1;
    /* The same ADC codes must shrink on screen within each relay stage,
       while their voltage remains constant. Exercise the real adapter and projection. */
    for(i=0;i<6;++i){
        demo_signal_accept_scale(&demo,0,control_scale_mv(i));
        demo_signal_accept_scale(&demo,1,control_scale_mv(i));
        capture_request_from_demo(&demo,&request);
        for(ch=0;ch<2;++ch){
            buffer.volts_per_code[ch]=request.adc_volts_per_code[ch];
            view.pixels_per_volt[ch]=request.pixels_per_volt[ch];
            view.zero_y[ch]=request.zero_y[ch];
            assert(fabs(buffer.volts_per_code[ch]-0.004*(1000000+lower[i])/lower[i])<1e-12);
        }
        assert(capture_process(&buffer,&view,&frame));
        /* Upsampling has no envelope at a single column; compare known extrema. */
        span[i]=fabs(frame.y[0][0]-frame.y[0][SCOPE_PLOT_WIDTH-1]);
        voltage[i]=frame.maximum[0]-frame.minimum[0];
    }
    assert(span[0]>span[1] && fabs(span[0]-2*span[1])<=2);
    assert(span[2]>span[3] && span[3]>span[4]);
    assert(fabs(span[2]-2*span[3])<=2 && fabs(span[3]-2*span[4])<=2);
    assert(voltage[0]==voltage[1] && voltage[2]==voltage[3] && voltage[3]==voltage[4]);
    demo_signal_set_option(&demo,DEMO_MENU_CH1,2,1);
    capture_request_from_demo(&demo,&request);
    assert(fabs(request.adc_volts_per_code[0]-buffer.volts_per_code[0]*10)<1e-12);
    assert(fabs(request.adc_volts_per_code[0]*request.pixels_per_volt[0]-
                buffer.volts_per_code[0]*view.pixels_per_volt[0])<1e-12);
}

static void test_menu_button_hold_release(void)
{
    static const DemoControl buttons[] = {
        DEMO_BTN_CH1, DEMO_BTN_CH2, DEMO_BTN_CURSOR, DEMO_BTN_MODE, DEMO_BTN_MENU
    };
    static const DemoMenu menus[] = {
        DEMO_MENU_CH1, DEMO_MENU_CH2, DEMO_MENU_CURSOR, DEMO_MENU_TRIGGER, DEMO_MENU_MAIN
    };
    unsigned i;
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        HeldControl held = {0};
        demo_signal_init(&demo);
        pending_chord = DEMO_CONTROL_COUNT;
        demo_signal_ui_open_menu(&demo, menus[i]);
        begin_press(&held, buttons[i], 0, 1000);
        fire_long_press(&held, 1299);
        assert(demo.screen.menu_open && !held.long_done);
        fire_long_press(&held, 1300);
        assert(!demo.screen.menu_open && held.long_done);
        finish_press(&held, 1310);
        assert(!demo.screen.menu_open && demo.screen.ch1_enabled && demo.screen.ch2_enabled);
        assert(demo.screen.cursor_mode == SCOPE_CURSOR_OFF && !demo.screen.zoom_enabled);
        assert(demo.trigger_mode_index == 0);
    }
}

static void test_service_power_clock(void)
{
    char directory[]="/tmp/oscill-status-frontend-XXXXXX",path[256];
    assert(mkdtemp(directory));
    snprintf(path,sizeof(path),"%s/status.sock",directory);
    int receiver=status_receiver_open(path),sender=status_sender_open();
    assert(receiver>=0 && sender>=0);
    demo_signal_init(&demo);
    serial_controls.metrics.power_mv=4123;
    serial_controls.metrics.power_received_ms=scope_clock_ns()/1000000-1500;
    serial_controls.metrics.temperature_mc=-12500;
    serial_controls.metrics.temperature_valid=1;
    serial_controls.metrics.temperature_received_ms=scope_clock_ns()/1000000-500;
    publish_service_status(sender,path,STATUS_READY,STATUS_DEMO,1,1,60,120000000);
    StatusPacket packet;
    assert(status_receive(receiver,&packet)==1);
    assert(packet.power_mv==4123 && packet.power_age_ms>=1500 && packet.power_age_ms<2500);
    assert(packet.source==STATUS_DEMO && !packet.pipeline && packet.frames==60);
    assert(packet.stm_temperature_valid && packet.stm_temperature_mc==-12500 &&
           packet.stm_temperature_age_ms>=500 && packet.stm_temperature_age_ms<1500);
    serial_controls.metrics.temperature_valid=0;
    publish_service_status(sender,path,STATUS_READY,STATUS_DEMO,1,1,60,120000000);
    assert(status_receive(receiver,&packet)==1 && !packet.stm_temperature_valid);
    memset(&serial_controls.metrics,0,sizeof(serial_controls.metrics));
    close(sender);close(receiver);assert(unlink(path)==0);assert(rmdir(directory)==0);
}

static void test_console_baud_and_status(void)
{
    unsigned actual;
    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    console_path = ptsname(master);
    assert(console_path && linux_serial_set_baud(console_path, 115200));
    demo_signal_init(&demo);
    demo_signal_pc_status(&demo, 1, 115200, 0);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PC);
    assert(demo_signal_set_option(&demo, DEMO_MENU_PC, 0, 8));
    update_pc_console(100);
    assert(linux_serial_get_baud(console_path, &actual) && actual == 1000000);
    assert(strcmp(demo.screen.menu_values[1], "USB-UART READY") == 0);
    assert(demo_signal_set_option(&demo, DEMO_MENU_PC, 0, 9));
    update_pc_console(101);
    assert(linux_serial_get_baud(console_path, &actual) && actual == 2000000);
    assert(!linux_serial_set_baud(console_path, 12345));
    assert(linux_serial_get_baud(console_path, &actual) && actual == 2000000);
    console_path = "/no/such/tty";
    assert(demo_signal_set_option(&demo, DEMO_MENU_PC, 0, 4));
    update_pc_console(101);
    assert(demo_signal_pc_baud(demo.pc_baud_index) == 2000000);
    assert(strcmp(demo.screen.status_message, "UART BAUD FAILED") == 0);
    close(master);
    console_path = NULL;
    console_baud = 115200;
}

static int scale_request_count, scale_request_mv;
static int test_scale_request(void *context, unsigned channel, int millivolts)
{
    assert(context == &demo && channel == 0);
    ++scale_request_count;
    scale_request_mv = millivolts;
    return 1;
}

static void test_panel_owned_scale(void)
{
    demo_signal_init(&demo);
    demo_signal_bind_scale_control(&demo, test_scale_request, &demo);
    demo_signal_zoom_channel(&demo, 0, 1);
    assert(scale_request_count == 1 && scale_request_mv == 1000);
    assert(demo.scale_index[0] == 2);
    demo_signal_accept_scale(&demo, 0, 1000);
    assert(demo.scale_index[0] == 3 && scale_request_count == 1);
    demo_signal_set_option(&demo, DEMO_MENU_CH1, 3, 5);
    assert(scale_request_count == 2 && scale_request_mv == 5000);
    assert(demo.scale_index[0] == 3);
    demo_signal_accept_scale(&demo, 0, 5000);
    assert(demo.scale_index[0] == 5);
    demo_signal_set_option(&demo, DEMO_MENU_CH1, 2, 1);
    assert(demo.probe_ten[0] == 1 && scale_request_count == 2);
    demo_signal_accept_scale(&demo, 0, 123);
    assert(demo.scale_index[0] == 5);
    demo_signal_bind_scale_control(&demo, NULL, NULL);
}

static void test_fft_fine_touch(void)
{
    int mode;
    for(mode=SCOPE_CURSOR_TIME;mode<=SCOPE_CURSOR_VOLTAGE;++mode){
        int x=300,y,initial,delta,coarse;
        demo_signal_init(&demo);
        demo_signal_ui_open_menu(&demo,DEMO_MENU_PROCESSING);
        demo_signal_ui_menu_choose(&demo,4,1);
        demo_signal_ui_dismiss_menu(&demo);
        demo_signal_ui_cycle_cursor_mode(&demo);
        y=scope_screen_spectrum_top(&demo.screen)+85;
        if(mode==SCOPE_CURSOR_VOLTAGE)demo_signal_ui_cycle_cursor_mode(&demo);
        lcd_touch_begin(x,y);
        assert(touch.zone==TOUCH_FFT_CURSOR);
        initial=mode==SCOPE_CURSOR_TIME?demo.screen.fft_cursor_x:demo.screen.fft_cursor_level;
        touch_move(x+8,y-8);
        coarse=(mode==SCOPE_CURSOR_TIME?demo.screen.fft_cursor_x:demo.screen.fft_cursor_level)-initial;
        lcd_touch_end(y-8);
        demo_signal_ui_move_fft_cursor(&demo,initial);
        demo_signal_ui_toggle_fine(&demo);
        lcd_touch_begin(x+20,y+3);
        /* Grabbing in FINE selects the cursor without jumping to the finger. */
        assert((mode==SCOPE_CURSOR_TIME?demo.screen.fft_cursor_x:demo.screen.fft_cursor_level)==initial);
        touch_move(x+28,y-5);
        delta=mode==SCOPE_CURSOR_TIME?8:scope_screen_fft_level_at(&demo.screen,y-5)-scope_screen_fft_level_at(&demo.screen,y+3);
        assert((mode==SCOPE_CURSOR_TIME?demo.screen.fft_cursor_x:demo.screen.fft_cursor_level)==initial+delta/4);
        assert(abs(delta/4)<abs(coarse));
        touch_move(x+20,y+3);
        assert((mode==SCOPE_CURSOR_TIME?demo.screen.fft_cursor_x:demo.screen.fft_cursor_level)==initial);
        lcd_touch_end(y+3);
    }
}

static void test_remote_commands(void)
{
    FILE *response;int previous=demo.time_index;char header[32];
    response=tmpfile();assert(response);
    assert(remote_command("set record_depth_index 4",response,NULL));
    assert(demo.record_depth_index==4);fclose(response);
    response=tmpfile();assert(response);
    assert(remote_command("set time_index 4",response,NULL));
    assert(demo.time_index!=previous);fclose(response);
    previous=demo.time_index;response=tmpfile();assert(response);
    assert(!remote_command("set time_index 4 trailing",response,NULL));
    assert(!remote_command("set time_index 5000",response,NULL));
    assert(!remote_command("set trigger_mode 3",response,NULL));
    assert(demo.time_index==previous);fclose(response);
    response=tmpfile();assert(response);assert(remote_command("csv",response,NULL));
    rewind(response);assert(fgets(header,sizeof(header),response));assert(!strcmp(header,"#oscill_wave=2\n"));fclose(response);
    response=tmpfile();assert(response);assert(remote_command("state",response,NULL));
    rewind(response);assert(fread(header,1,1,response)==1);assert(header[0]=='{');fclose(response);
}

static void test_delayed_panel_release(void)
{
    ControlEvent event={CONTROL_DOWN,DEMO_BTN_MENU,0,100,0};
    control_queue_init(&controls);
    memset(key_holds,0,sizeof(key_holds));pending_chord=DEMO_CONTROL_COUNT;
    demo_signal_init(&demo);
    assert(control_queue_push(&controls,&event));
    drain_controls(1000);
    event.type=CONTROL_UP;event.timestamp_ms=180;
    assert(control_queue_push(&controls,&event));
    /* UI was busy for a second, but the physical press lasted only 80 ms. */
    drain_controls(2000);
    assert(demo.screen.menu_open && demo.menu_kind==DEMO_MENU_MAIN);
    assert(!key_holds[DEMO_BTN_MENU].active);
    control_queue_destroy(&controls);
}

static void test_panel_battery(void)
{
    SerialControlMetrics panel={0};
    demo_signal_init(&demo);
    panel.connected=1;panel.power_mv=3850;panel.power_received_ms=1000;
    assert(update_panel_battery(&panel,1000));
    assert(demo.screen.battery_known && demo.screen.battery_percent==60);
    assert(!update_panel_battery(&panel,2000));
    assert(update_panel_battery(&panel,4001) && !demo.screen.battery_known);
    panel.power_received_ms=5000;
    assert(update_panel_battery(&panel,5000) && demo.screen.battery_known);
    panel.connected=0;
    assert(update_panel_battery(&panel,5001) && !demo.screen.battery_known);
}

int main(void)
{
    CaptureMetrics pending={0};pending.waiting=1;
    assert(capture_waiting(&pending,0,1,1));
    pending.triggered_ns=1000000000;pending.processing_ns=9000000000ULL;
    assert(!capture_waiting(&pending,0,1,1)); /* Neither processing nor painting is required. */
    assert(!capture_waiting(&pending,999999999,1,1));
    assert(capture_waiting(&pending,1000000000,1,1)); /* Only the matching displayed plot completes RUN. */
    assert(!capture_waiting(&pending,1000000000,0,1));
    assert(!capture_waiting(&pending,1000000000,1,0));
    test_restored_settings();
    test_response_capture();
    test_delayed_panel_release();
    test_panel_battery();
    test_debug_hold_threshold();
    test_external_trigger_marker();
    test_adc_vertical_ranges();
    test_menu_button_hold_release();
    char directory[]="/dev/shm/oscill-export-XXXXXX", path[PATH_MAX];
    struct stat info;
    uint32_t *expected;
    int channel,x;
    screen_pixels=malloc(SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*screen_pixels));
    expected=malloc(SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*expected));
    assert(screen_pixels && expected);
    panel_enabled=0;
#ifdef SCOPE_ENABLE_ZYNQ
    test_startup_retry();
#endif
    test_service_power_clock();
    test_console_baud_and_status();
    test_fft_fine_touch();
    test_remote_commands();
    test_panel_owned_scale();
    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo,-6); /* Default 10 kHz signal at 10 ms/div. */
    assert(strcmp(demo.screen.time_scale,"10 ms")==0);
    for(channel=0;channel<2;++channel)
        for(x=0;x<SCOPE_PLOT_WIDTH;++x){
            size_t i;
            double low=demo.source_samples[channel][8*x], high=low;
            double scale=channel?60.0:120.0, zero=channel?330.0:200.0;
            for(i=8*x+1;i<8*(size_t)(x+1);++i){
                if(demo.source_samples[channel][i]<low)low=demo.source_samples[channel][i];
                if(demo.source_samples[channel][i]>high)high=demo.source_samples[channel][i];
            }
            assert(demo.minimum[channel][x]==(int16_t)(zero-high*scale));
            assert(demo.maximum[channel][x]==(int16_t)(zero-low*scale));
        }
    scope_screen_render(expected,SCOPE_WIDTH,&demo.screen);
    draw_all();
    assert(memcmp(screen_pixels,expected,SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*expected))==0);
    assert(demo.screen.ch1_min_samples==demo.minimum[0]);
    assert(demo.screen.ch2_max_samples==demo.maximum[1]);
    /* Recover demo envelopes after displaying a raw capture as well. */
    demo.screen.ch1_min_samples=capture_frame.y_min[0];
    demo.screen.ch2_max_samples=capture_frame.y_max[1];
    draw_all();
    assert(memcmp(screen_pixels,expected,SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*expected))==0);
    assert(mkdtemp(directory));
    strcpy(application_dir,directory);
    assert(linux_storage_check(directory,0));
    assert(!linux_storage_check(directory,1));
    assert(chmod(directory,0555)==0);
    assert(!linux_storage_check(directory,0));
    assert(chmod(directory,0700)==0);
    require_data_device=1;
    act(DEMO_ACTION_SCREENSHOT);
    assert(strcmp(demo.screen.status_message,"NO DATA CARD")==0);
    act(DEMO_ACTION_SCREEN_AND_WAVE);
    assert(strcmp(demo.screen.status_message,"NO DATA CARD")==0);
    snprintf(path,sizeof(path),"%s/capture_0001.bmp",directory);
    assert(stat(path,&info)!=0);
    /* Desktop preview may explicitly save to an ordinary directory. */
    require_data_device=0;
    act(DEMO_ACTION_SCREEN_AND_WAVE);
    assert(strcmp(demo.screen.status_message,"SCREEN AND WAVE SAVED")==0);
    assert(stat(path,&info)==0 && info.st_size==54+SCOPE_WIDTH*SCOPE_HEIGHT*4);
    assert(remove(path)==0);
    snprintf(path,sizeof(path),"%s/capture_0001.csv",directory);
    assert(stat(path,&info)==0 && info.st_size>0);
    assert(load_browse_wave(path));
    assert(remove(path)==0);
    assert(rmdir(directory)==0);
    free(expected);free(screen_pixels);
    return 0;
}
