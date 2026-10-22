#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#define main scope_preview_main
#include "../preview_sdl.c"
#undef main
#include <assert.h>
#include <fcntl.h>

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
    assert(!linux_serial_set_baud(console_path, 12345));
    assert(linux_serial_get_baud(console_path, &actual) && actual == 1000000);
    console_path = "/no/such/tty";
    assert(demo_signal_set_option(&demo, DEMO_MENU_PC, 0, 4));
    update_pc_console(101);
    assert(demo_signal_pc_baud(demo.pc_baud_index) == 1000000);
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
        int x=300,y=SCOPE_PLOT_Y+45,initial,delta,coarse;
        demo_signal_init(&demo);
        demo_signal_ui_open_menu(&demo,DEMO_MENU_PROCESSING);
        demo_signal_ui_menu_choose(&demo,4,1);
        demo_signal_ui_dismiss_menu(&demo);
        demo_signal_ui_cycle_cursor_mode(&demo);
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

int main(void)
{
    char directory[]="/dev/shm/oscill-export-XXXXXX", path[PATH_MAX];
    struct stat info;
    uint32_t *expected;
    int channel,x;
    screen_pixels=malloc(SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*screen_pixels));
    expected=malloc(SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*expected));
    assert(screen_pixels && expected);
    panel_enabled=0;
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
