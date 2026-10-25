#ifdef NDEBUG
#undef NDEBUG
#endif
#include "demo_signal.h"
#include "rounded_box.h"
#include "boot_splash.h"
#include "waveform_view.h"
#include "spectrum_bins.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint32_t corners[68 * 68];
static RoundedBox box;
static DemoSignal demo;

static void test_spectrum_projection(void)
{
    static double re[4096],im[4096];
    double maximum=0;unsigned fractional=0;
    re[3]=1;
    for(unsigned column=0;column<1024;++column){
        double value=spectrum_project(re,im,16,column,1024);
        assert(value>=0 && value<=1);
        if(value>maximum)maximum=value;
        if(value>0.05 && value<0.95)++fractional;
    }
    assert(maximum==1 && fractional>20);
    memset(re,0,sizeof(re));re[1234]=3;maximum=0;
    for(unsigned column=0;column<32;++column){double value=spectrum_project(re,im,4096,column,32);if(value>maximum)maximum=value;}
    assert(maximum==3);
}

static void test_cursor_table_and_fft_overlay(uint32_t *pixels, int stride)
{
    int y, width;
    demo_signal_init(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    scope_screen_cursor_measurement_bounds(&demo.screen, NULL, &y, &width, NULL);
    scope_screen_render(pixels, stride, &demo.screen);
    /* The common frame continues through the boundary between CH1 and CH2. */
    assert(pixels[(y + 33) * stride] == 0x997ec4);
    assert(pixels[(y + 33) * stride + width - 1] == 0x997ec4);
    /* Both channel labels use the same cursor color as the TIME row. */
    {
        int row, x, yy, colored;
        for (row = 0; row < 2; ++row) {
            colored = 0;
            for (yy = y + row * 33 + 9; yy < y + row * 33 + 23; ++yy)
                for (x = 12; x < 54; ++x) {
                    uint32_t pixel = pixels[yy * stride + x];
                    assert(pixel != 0xffc44d && pixel != 0x41c7e2);
                    if (pixel == 0xcfb7ff) colored = 1;
                }
            assert(colored);
        }
    }

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    memset(demo.fft_ch1, 0, sizeof(demo.fft_ch1));
    demo.screen.ch2_enabled = 0;
    scope_screen_render(pixels, stride, &demo.screen);
    demo.screen.measurement_hidden=1;demo.screen.measurement_x=800;
    scope_screen_render(pixels,stride,&demo.screen);
    /* The FFT baseline reaches the same band as frequency labels. */
    y = scope_screen_spectrum_top(&demo.screen) + scope_screen_spectrum_height(&demo.screen) - 5;
    assert(pixels[y * stride + 700] == 0xffc44d);
}

static void test_trace_width(uint32_t *pixels, int stride)
{
    int zoom, split, x, y, colored;
    demo_signal_init(&demo);
    demo.screen.ch2_enabled = 0;
    demo.screen.measurement_count = 0;
    demo.screen.channel_zero_y[0] = 350;
    demo.screen.density_source[0].samples=0;
    for (x = 0; x < SCOPE_PLOT_WIDTH; ++x)
        demo.ch1[x] = demo.minimum[0][x] = demo.maximum[0][x] = 210;
    for (zoom = 0; zoom < 2; ++zoom) {
        demo.screen.zoom_enabled = (uint8_t)zoom;
        for (split = 70; split <= 250; split += 90) {
            demo.screen.split_height = split;
            scope_screen_render(pixels, stride, &demo.screen);
            colored = 0;
            {
                int top = SCOPE_PLOT_Y + (zoom ? split : 0);
                int height = SCOPE_PLOT_HEIGHT - (zoom ? split : 0);
                int expected = top + 210 * height / SCOPE_PLOT_HEIGHT;
                for (y = expected - 6; y <= expected + 6; ++y)
                    if (pixels[y * stride + 100] == 0xffc44d) ++colored;
            }
            assert(colored == 1); /* One monitor pixel in every layout. */
        }
    }
}

static void test_external_without_samples(uint32_t *pixels, int stride)
{
    int x, y, ch;
    static uint32_t expected[SCOPE_WIDTH * SCOPE_HEIGHT];
    demo_signal_init(&demo);
    demo_signal_begin_external(&demo);
    demo.screen.running = 0; /* source error before the first DMA block */
    demo_signal_zoom_time(&demo, 1);
    demo_signal_move_channel(&demo, 0, 20);
    demo_signal_ui_toggle_zoom(&demo);
    assert(demo.external_capture && !demo.external_frame_valid && !demo.screen.history_count);
    for(x=0;x<SCOPE_PLOT_WIDTH;++x) {
        assert(demo.ch1[x]==WAVEFORM_NO_SAMPLE && demo.ch2[x]==WAVEFORM_NO_SAMPLE);
        for(ch=0;ch<2;++ch)
            assert(demo.minimum[ch][x]==WAVEFORM_NO_SAMPLE && demo.maximum[ch][x]==WAVEFORM_NO_SAMPLE);
    }
    demo.screen.measurement_count=0;
    scope_screen_render(pixels,stride,&demo.screen);
    demo.screen.ch1_enabled=demo.screen.ch2_enabled=0;
    scope_screen_render(expected,SCOPE_WIDTH,&demo.screen);
    for(y=SCOPE_PLOT_Y+20;y<SCOPE_PLOT_Y+SCOPE_PLOT_HEIGHT-20;++y)
        for(x=40;x<SCOPE_PLOT_WIDTH-40;++x)
            assert(pixels[y*stride+x]==expected[y*SCOPE_WIDTH+x]);
    demo_signal_ui_open_menu(&demo,DEMO_MENU_MAIN);
    assert(demo.screen.menu_open);
}

static void test_trace_gaps(uint32_t *pixels, int stride)
{
    static uint8_t density[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    int mode,x;
    demo_signal_init(&demo);demo.screen.ch2_enabled=0;demo.screen.measurement_count=0;
    demo.screen.channel_zero_y[0]=350;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        int16_t value=x>=256 && x<768 && !(x>=500 && x<520)?210:WAVEFORM_NO_SAMPLE;
        demo.ch1[x]=demo.minimum[0][x]=demo.maximum[0][x]=value;
        demo.history_ch1[0][x]=demo.history_minimum[0][0][x]=demo.history_maximum[0][0][x]=value;
        if(value!=WAVEFORM_NO_SAMPLE)density[x][value]=255;
    }
    demo.screen.density_levels[0]=&density[0][0];
    for(mode=0;mode<3;++mode){
        demo.screen.ch1_min_samples=mode?demo.minimum[0]:NULL;
        demo.screen.ch1_max_samples=mode?demo.maximum[0]:NULL;
        demo.screen.intensity_coloring=(uint8_t)(mode==2);demo.screen.history_count=1;
        scope_screen_render(pixels,stride,&demo.screen);
        uint32_t at_gap=pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+510];
        uint32_t outside=pixels[(SCOPE_PLOT_Y+3)*stride+SCOPE_PLOT_X+100];
        uint32_t signal=pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+300];
        demo.screen.ch1_enabled=0;scope_screen_render(pixels,stride,&demo.screen);
        assert(at_gap==pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+510]);
        assert(outside==pixels[(SCOPE_PLOT_Y+3)*stride+SCOPE_PLOT_X+100]);
        assert(signal!=pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+300]);
        demo.screen.ch1_enabled=1;
    }
}

static void test_density_connections_and_battery(uint32_t *pixels, int stride)
{
    static uint8_t density[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    int x,y;
    demo_signal_init(&demo);
    demo.screen.ch2_enabled=0;demo.screen.measurement_count=0;
    demo.screen.channel_zero_y[0]=350;
    demo.screen.intensity_coloring=1;demo.screen.peak_detect=0;
    demo.screen.density_levels[0]=&density[0][0];
    demo.screen.density_rows=SCOPE_PLOT_HEIGHT;demo.screen.density_samples=64;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x)demo.ch1[x]=210;
    density[300][210]=density[316][210]=255;
    demo.screen.battery_known=1;demo.screen.battery_percent=0;
    scope_screen_render(pixels,stride,&demo.screen);
    assert(pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+300]==0xff1914);
    assert(pixels[(SCOPE_PLOT_Y+210)*stride+SCOPE_PLOT_X+308]==0x002396);
    /* Empty battery has no number drawn over its interior. */
    for(y=15;y<37;++y)for(x=158;x<171;++x)assert(pixels[y*stride+x]!=0xe5f0f2);
    demo.screen.battery_percent=100;
    scope_screen_render(pixels,stride,&demo.screen);
    assert(pixels[34*stride+160]==0x70dfa3);
}

static void test_plain_noise_intensity(uint32_t *pixels,int stride)
{
    static uint8_t density[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    static uint32_t background[SCOPE_WIDTH*SCOPE_HEIGHT];
    static int16_t hidden_min[SCOPE_PLOT_WIDTH],hidden_max[SCOPE_PLOT_WIDTH];
    static uint8_t strength[SCOPE_PLOT_WIDTH];
    int x,y;
    demo_signal_init(&demo);demo.screen.ch2_enabled=0;demo.screen.measurement_count=0;
    demo.screen.channel_zero_y[0]=350;demo.screen.intensity_coloring=0;
    demo.screen.density_levels[0]=&density[0][0];demo.screen.density_rows=SCOPE_PLOT_HEIGHT;
    demo.screen.density_samples=8192;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        demo.ch1[x]=210;demo.minimum[0][x]=180;demo.maximum[0][x]=240;
        density[x][210]=255;density[x][180]=16;
        hidden_min[x]=hidden_max[x]=WAVEFORM_NO_SAMPLE;
    }
    scope_screen_render(pixels,stride,&demo.screen);
    demo.screen.ch1_enabled=0;scope_screen_render(background,SCOPE_WIDTH,&demo.screen);
    x=300;
    assert(pixels[(SCOPE_PLOT_Y+210)*stride+x]==0xffc44d);
    assert(pixels[(SCOPE_PLOT_Y+180)*stride+x]!=background[(SCOPE_PLOT_Y+180)*SCOPE_WIDTH+x]);
    assert(pixels[(SCOPE_PLOT_Y+180)*stride+x]!=0xffc44d);
    /* There is no solid envelope between noise hits and no extra stroke halo. */
    for(y=181;y<240;++y)if(y!=210)
        assert(pixels[(SCOPE_PLOT_Y+y)*stride+x]==background[(SCOPE_PLOT_Y+y)*SCOPE_WIDTH+x]);
    demo.screen.ch1_enabled=1;demo.screen.peak_detect=1;
    demo.screen.peak_min_samples[0]=hidden_min;demo.screen.peak_max_samples[0]=hidden_max;
    demo.screen.peak_strength[0]=strength;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        hidden_min[x]=180;hidden_max[x]=240;
        strength[x]=255;
        demo.minimum[0][x]=demo.maximum[0][x]=210;
    }
    scope_screen_render(pixels,stride,&demo.screen);
    /* True hidden excursions still show a wall independently of hit density. */
    assert(pixels[(SCOPE_PLOT_Y+190)*stride+300]!=background[(SCOPE_PLOT_Y+190)*SCOPE_WIDTH+300]);
    {
        const unsigned spans[]={512,1024,2048,4096};unsigned i,previous=0;
        for(i=0;i<4;++i){
            unsigned value;
            demo.screen.density_samples=spans[i];scope_screen_render(pixels,stride,&demo.screen);
            value=(pixels[(SCOPE_PLOT_Y+190)*stride+300]>>16)&255;
            assert(i==0 || value>previous);previous=value;
        }
    }
    demo.screen.density_samples=512;
    scope_screen_render(pixels,stride,&demo.screen);
    /* At two pixels per sample and closer, the background disappears while
       the actual curve remains fully visible and connected. */
    assert(pixels[(SCOPE_PLOT_Y+190)*stride+300]==background[(SCOPE_PLOT_Y+190)*SCOPE_WIDTH+300]);
    assert(pixels[(SCOPE_PLOT_Y+210)*stride+308]==0xffc44d);
    demo.screen.density_samples=64;
    scope_screen_render(pixels,stride,&demo.screen);
    assert(pixels[(SCOPE_PLOT_Y+190)*stride+300]==background[(SCOPE_PLOT_Y+190)*SCOPE_WIDTH+300]);
    assert(pixels[(SCOPE_PLOT_Y+210)*stride+308]==0xffc44d);
    demo.screen.peak_detect=0;
}

static void test_connected_front(uint32_t *pixels,int stride)
{
    static uint8_t hits[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    static uint8_t coverage[SCOPE_PLOT_WIDTH][SCOPE_PLOT_HEIGHT];
    static uint8_t strength[SCOPE_PLOT_WIDTH];
    static uint32_t background[SCOPE_WIDTH*SCOPE_HEIGHT];
    int ch,mode,x,y;
    demo_signal_init(&demo);demo.screen.measurement_count=0;
    demo.screen.density_rows=SCOPE_PLOT_HEIGHT;demo.screen.density_samples=8192;
    demo.screen.peak_detect=1;
    for(x=0;x<SCOPE_PLOT_WIDTH;++x){
        demo.ch1[x]=demo.ch2[x]=x<300?180:340;
        hits[x][x<300?180:340]=255;
        strength[x]=255; /* Missing native extrema must not erase a real edge. */
    }
    for(y=180;y<=340;++y){coverage[300][y]=64;if(y%24==0)hits[300][y]=16;}
    demo.screen.ch1_enabled=demo.screen.ch2_enabled=0;
    scope_screen_render(background,SCOPE_WIDTH,&demo.screen);
    for(ch=0;ch<2;++ch)for(mode=0;mode<2;++mode){
        demo.screen.ch1_enabled=ch==0;demo.screen.ch2_enabled=ch==1;
        demo.screen.intensity_coloring=(uint8_t)mode;
        demo.screen.connections[ch]=&coverage[0][0];
        demo.screen.density_levels[ch]=&hits[0][0];
        demo.screen.peak_strength[ch]=strength;
        scope_screen_render(pixels,stride,&demo.screen);
        for(y=181;y<340;++y)
            assert(pixels[(SCOPE_PLOT_Y+y)*stride+300]!=background[(SCOPE_PLOT_Y+y)*SCOPE_WIDTH+300]);
        if(mode)assert((pixels[(SCOPE_PLOT_Y+201)*stride+300]&255)>=90);
        /* The adjacent column has no recorded transition: do not fill it. */
        for(y=200;y<320;++y)
            assert(pixels[(SCOPE_PLOT_Y+y)*stride+299]==background[(SCOPE_PLOT_Y+y)*SCOPE_WIDTH+299]);
    }
}

static void test_fft_cursor_label(uint32_t *pixels, int stride)
{
    int side, x, y;
    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    demo_signal_ui_cycle_cursor_mode(&demo);
    for (side = 0; side < 2; ++side) {
        int colored = 0;
        int column = side ? 1023 : 100;
        demo_signal_ui_move_fft_cursor(&demo, column);
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = scope_screen_spectrum_top(&demo.screen) + 57; y < scope_screen_spectrum_top(&demo.screen) + 71; ++y)
            for (x = side ? 800 : 108; x < (side ? 1015 : 280); ++x)
                if (pixels[y * stride + x] == 0xcfb7ff) ++colored;
        assert(colored > 20); /* Label is visible on the available side. */
    }
    demo_signal_ui_move_fft_cursor(&demo, 512);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(scope_screen_spectrum_top(&demo.screen) + 59) * stride + 512] == 0xcfb7ff);
    assert(pixels[(scope_screen_spectrum_top(&demo.screen) + 49) * stride + 512] == 0xcfb7ff);
    assert(pixels[(scope_screen_spectrum_top(&demo.screen) + 101) * stride + 512] == 0xcfb7ff);
    demo_signal_ui_set_split_height(&demo, 70);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(scope_screen_spectrum_top(&demo.screen) + 59) * stride + 512] == 0xcfb7ff);
    demo_signal_ui_cycle_cursor_mode(&demo);
    demo_signal_ui_move_fft_cursor(&demo, 128);
    y = scope_screen_spectrum_top(&demo.screen) + scope_screen_spectrum_height(&demo.screen) - 5 -
        128 * (scope_screen_spectrum_height(&demo.screen) - 53) / 255;
    assert(scope_screen_fft_level_at(&demo.screen, y) >= 128);
    assert(scope_screen_fft_level_at(&demo.screen, -1000) == 255);
    assert(scope_screen_fft_level_at(&demo.screen, 1000) == 0);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[y * stride + 201] == 0xcfb7ff);
    assert(pixels[y * stride + 801] == 0xcfb7ff);
    demo_signal_ui_toggle_zoom(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[(scope_screen_spectrum_top(&demo.screen) + 59) * stride + 512] != 0xcfb7ff);
}

static void test_cursor_card_selection(uint32_t *pixels, int stride)
{
    uint32_t mode_pixels[200 * 28];
    int font, loaded, mode, selected, x, y;
    demo_signal_init(&demo);
    for (font = 0; font < SCOPE_FONT_COUNT; ++font) {
        demo.screen.font_index = (uint8_t)font;
        for (loaded = 0; loaded < 2; ++loaded) {
            demo.screen.waveform_loaded = (uint8_t)loaded;
            for (mode = SCOPE_CURSOR_TIME; mode <= SCOPE_CURSOR_VOLTAGE; ++mode) {
                demo.screen.cursor_mode = (uint8_t)mode;
                for (selected = SCOPE_CURSOR_SELECT_A; selected <= SCOPE_CURSOR_SELECT_FFT; ++selected) {
                    int colored = 0;
                    demo.screen.cursor_selected = (uint8_t)selected;
                    scope_screen_render(pixels, stride, &demo.screen);
                    /* TIME / VERTICAL stays unchanged for all three selections. */
                    for (y = 0; y < 28; ++y)
                        for (x = 0; x < 200; ++x) {
                            uint32_t pixel = pixels[(562 + y) * stride + 814 + x];
                            if (selected == SCOPE_CURSOR_SELECT_A) mode_pixels[y * 200 + x] = pixel;
                            else assert(pixel == mode_pixels[y * 200 + x]);
                        }
                    /* Each selection is shown separately beside the CURSORS heading. */
                    for (y = 543; y < 557; ++y)
                        for (x = 970; x < 1008; ++x)
                            if (pixels[y * stride + x] == 0xb696f1) ++colored;
                    assert(colored > 10);
                }
            }
        }
    }
}

static void test_lowercase_n(uint32_t *pixels, int stride)
{
    uint32_t lowercase[32 * 28];
    int font, x, y, different;
    demo_signal_init(&demo);
    for (font = 0; font < SCOPE_FONT_COUNT; ++font) {
        demo.screen.font_index = (uint8_t)font;
        demo.screen.time_scale = "n";
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = 0; y < 28; ++y)
            for (x = 0; x < 32; ++x)
                lowercase[y * 32 + x] = pixels[(562 + y) * stride + 415 + x];
        demo.screen.time_scale = "N";
        scope_screen_render(pixels, stride, &demo.screen);
        different = 0;
        for (y = 0; y < 28; ++y)
            for (x = 0; x < 32; ++x)
                different |= lowercase[y * 32 + x] != pixels[(562 + y) * stride + 415 + x];
        assert(different);
    }
}

static void test_trigger_icons(uint32_t *pixels, int stride)
{
    uint8_t both[20 * 12];
    int x, y, falling;
    demo_signal_init(&demo);
    demo.screen.trigger_edge_both = 1;
    scope_screen_render(pixels, stride, &demo.screen);
    for (y = 0; y < 12; ++y)
        for (x = 0; x < 20; ++x)
            both[y * 20 + x] = pixels[(545 + y) * stride + 778 + x] == 0xffc44d;
    for (falling = 0; falling < 2; ++falling) {
        demo.screen.trigger_edge_both = 0;
        demo.screen.trigger_edge_falling = (uint8_t)falling;
        scope_screen_render(pixels, stride, &demo.screen);
        for (y = 0; y < 12; ++y)
            for (x = 0; x < 10; ++x) {
                if (x < 5) {
                    assert(pixels[(545 + y) * stride + 778 + x] != 0xffc44d);
                    assert(pixels[(545 + y) * stride + 793 + x] != 0xffc44d);
                }
                assert((pixels[(545 + y) * stride + 783 + x] == 0xffc44d) ==
                       both[y * 20 + falling * 10 + x]);
            }
    }
}

static void test_buttons_and_overlay(uint32_t *pixels, int stride)
{
    uint32_t before;
    demo_signal_init(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0x70dfa3);
    demo_signal_ui_toggle_run(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0xed555f);
    demo.screen.waveform_loaded = 1;
    scope_screen_render(pixels, stride, &demo.screen);
    assert(pixels[40 * stride + 865] == 0xffc44d);

    demo_signal_init(&demo);
    scope_screen_render(pixels, stride, &demo.screen);
    before = pixels[550 * stride + 600];
    demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN);
    demo_signal_ui_move_menu(&demo, 0, 1000);
    scope_screen_render(pixels, stride, &demo.screen);
    /* Last menu row covers the bottom cards when dragged down. */
    assert(pixels[550 * stride + 600] != before);
    assert(pixels[550 * stride + 600] == 0x191c2d);

    demo_signal_init(&demo);
    demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
    demo_signal_ui_menu_choose(&demo, 4, 1);
    demo_signal_ui_dismiss_menu(&demo);
    assert(strcmp(demo.screen.fft_frequency_labels[0], "0") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[1], "1.18k") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[8], "512k") == 0);
    assert(strcmp(demo.screen.fft_sampling_info, "FS 1.024M / DF 1k") == 0);
    demo_signal_set_fft_range(&demo, 100000000.0, 65536);
    assert(strcmp(demo.screen.fft_frequency_labels[0], "0") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[1], "8.14k") == 0);
    assert(strcmp(demo.screen.fft_sampling_info, "FS 200M / DF 3.052k") == 0);
    assert(strcmp(demo.screen.fft_frequency_labels[8], "100M") == 0);
}

static void test_persistence(uint32_t *pixels,int stride)
{
    uint32_t background;
    demo_signal_init(&demo);demo.screen.ch2_enabled=0;
    demo.screen.ch1_samples=demo.history_ch1[0];
    demo.screen.ch1_min_samples=demo.screen.ch1_max_samples=NULL;
    demo.screen.density_source[0].samples=0;demo.screen.density_levels[0]=NULL;
    demo.screen.connections[0]=NULL;
    demo.screen.history_ch1_min_samples=demo.screen.history_ch1_max_samples=NULL;
    for(int age=0;age<8;++age)for(int x=0;x<SCOPE_PLOT_WIDTH;++x)demo.history_ch1[age][x]=240-age*17;
    demo.screen.history_count=1;scope_screen_render(pixels,stride,&demo.screen);
    background=pixels[(SCOPE_PLOT_Y+240-3*17)*stride+123];
    demo.screen.history_count=4;scope_screen_render(pixels,stride,&demo.screen);
    assert(pixels[(SCOPE_PLOT_Y+240-3*17)*stride+123]!=background);
    background=pixels[(SCOPE_PLOT_Y+240-6*17)*stride+123];
    demo.screen.history_count=8;scope_screen_render(pixels,stride,&demo.screen);
    assert(pixels[(SCOPE_PLOT_Y+240-6*17)*stride+123]!=background);
    assert(pixels[(SCOPE_PLOT_Y+240)*stride+123]==0xffc44d);
}

static void test_trigger_wait(uint32_t *pixels,int stride)
{
    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    demo.screen.running=1;demo.screen.waiting_for_trigger=0;
    scope_screen_render(pixels,stride,&demo.screen);assert(pixels[25*stride+860]==0x70dfa3);
    demo.screen.waiting_for_trigger=1;
    scope_screen_render(pixels,stride,&demo.screen);assert(pixels[25*stride+860]==0xffc44d);
    demo.screen.running=0;
    scope_screen_render(pixels,stride,&demo.screen);assert(pixels[25*stride+860]==0xed555f);
}

static void test_corner_composition(void)
{
    int i, x, y, partial = 0, edge = 0;
    const uint32_t base = 0x00203040, content = 0x00406080, border = 0x00ffffff;
    for (i = 0; i < 68 * 68; ++i) corners[i] = base;
    rounded_box_begin(&box, corners, 68, 68, 68, 2, 2, 64, 64, 32);
    for (y = 2; y < 66; ++y)
        for (x = 2; x < 66; ++x) corners[y * 68 + x] = content;
    box.border = border;
    box.border_alpha = 255;
    rounded_box_end(&box);
    assert(corners[2 * 68 + 2] == base);
    assert(corners[34 * 68 + 34] == content);
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            uint32_t color = corners[(2 + y) * 68 + 2 + x];
            /* Both lower corners and both sides must have identical coverage. */
            assert(color == corners[(65 - y) * 68 + 2 + x]);
            assert(color == corners[(2 + y) * 68 + 65 - x]);
            partial += color != base && color != content && color != border;
            edge += ((color >> 16) & 255) > 160;
        }
    assert(partial > 20 && edge > 20);
    /* No write outside the widget, even when its corners cross the viewport. */
    assert(corners[0] == base && corners[67 * 68 + 67] == base);
    rounded_box_begin(&box, corners, 68, 68, 68, -10, -10, 64, 64, 32);
    rounded_box_end(&box);
    assert(corners[67 * 68 + 67] == base);
}

int main(void)
{
    test_spectrum_projection();
    static const DemoMenu menus[] = {DEMO_MENU_MAIN, DEMO_MENU_DEBUG, DEMO_MENU_MEASURE,
                                    DEMO_MENU_CH1, DEMO_MENU_PROCESSING};
    uint32_t *pixels = malloc((SCOPE_WIDTH + 3) * SCOPE_HEIGHT * sizeof(*pixels));
    int mode, menu, x, width;
    assert(pixels);
    test_persistence(pixels,SCOPE_WIDTH+3);
    test_trigger_wait(pixels,SCOPE_WIDTH+3);
    test_corner_composition();
    test_trace_width(pixels, SCOPE_WIDTH + 3);
    test_trace_gaps(pixels, SCOPE_WIDTH + 3);
    test_density_connections_and_battery(pixels, SCOPE_WIDTH + 3);
    test_plain_noise_intensity(pixels, SCOPE_WIDTH + 3);
    test_connected_front(pixels, SCOPE_WIDTH + 3);
    test_external_without_samples(pixels, SCOPE_WIDTH + 3);
    test_trigger_icons(pixels, SCOPE_WIDTH + 3);
    test_lowercase_n(pixels, SCOPE_WIDTH + 3);
    test_cursor_table_and_fft_overlay(pixels, SCOPE_WIDTH + 3);
    test_fft_cursor_label(pixels, SCOPE_WIDTH + 3);
    test_cursor_card_selection(pixels, SCOPE_WIDTH + 3);
    test_buttons_and_overlay(pixels, SCOPE_WIDTH + 3);
    boot_splash_render(pixels, SCOPE_WIDTH + 3, SCOPE_WIDTH, SCOPE_HEIGHT);
    assert(pixels[0] == BOOT_SPLASH_BACKGROUND);
    assert(pixels[300 * (SCOPE_WIDTH + 3) + 512] != BOOT_SPLASH_BACKGROUND);
    for (mode = 0; mode < 3; ++mode) {
        demo_signal_init(&demo);
        for (x = 0; x < mode; ++x) demo_signal_ui_cycle_cursor_mode(&demo);
        scope_screen_measurement_bounds(&demo.screen, &x, NULL, &width, NULL);
        assert(x == 0);
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
        for (menu = 0; menu < sizeof(menus) / sizeof(menus[0]); ++menu) {
            demo_signal_ui_open_menu(&demo, menus[menu]);
            scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
            demo_signal_ui_move_menu(&demo, -2000, -2000);
            scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
            demo_signal_ui_dismiss_menu(&demo);
        }
        demo.screen.waveform_loaded = 1;
        demo.screen.browser_title = "TEST CAPTURE";
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
        demo.screen.browser_visible = 1;
        scope_screen_render(pixels, SCOPE_WIDTH + 3, &demo.screen);
    }
    scope_screen_render(NULL, 0, NULL);
    free(pixels);
    return 0;
}
