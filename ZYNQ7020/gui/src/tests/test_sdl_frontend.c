#ifdef NDEBUG
#undef NDEBUG
#endif
#define main scope_preview_main
#include "../preview_sdl.c"
#undef main
#include <assert.h>

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
    demo_signal_init(&demo);
    demo_signal_zoom_time(&demo,-6); /* Default 10 kHz signal at 10 ms/div. */
    assert(strcmp(demo.screen.time_scale,"10 ms")==0);
    for(channel=0;channel<2;++channel)
        for(x=0;x<SCOPE_PLOT_WIDTH;++x)
            assert(demo.maximum[channel][x]-demo.minimum[channel][x]>=95);
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
