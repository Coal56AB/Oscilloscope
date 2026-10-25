#ifdef NDEBUG
#undef NDEBUG
#endif
#include "scope_renderer.h"
#include "scope_thread.h"
#include "demo_signal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ScopeMutex mutex;
static ScopeCondition wake;
static int blocked,entered,output_result=1;
static int pins;
static int16_t borrowed[SCOPE_PLOT_WIDTH];
static DemoSignal demo;
static uint32_t expected[SCOPE_WIDTH*SCOPE_HEIGHT],actual[SCOPE_WIDTH*SCOPE_HEIGHT];
static uint32_t plot_expected[SCOPE_WIDTH*SCOPE_HEIGHT];

static void gated_render(uint32_t *pixels,int stride,const ScopeScreen *screen)
{
    scope_mutex_lock(&mutex);entered=1;scope_condition_signal(&wake);
    while(blocked)scope_condition_wait(&wake,&mutex,100);
    scope_mutex_unlock(&mutex);
    scope_screen_render(pixels,stride,screen);
}

static void wait_entered(void)
{
    scope_mutex_lock(&mutex);
    while(!entered)scope_condition_wait(&wake,&mutex,100);
    scope_mutex_unlock(&mutex);
}

static void gated_plot(uint32_t *pixels,int stride,const ScopeScreen *screen)
{
    ScopeScreen plot=*screen;plot.plot_only=1;plot.plot_pixels=NULL;
    gated_render(pixels,stride,&plot);
}

static void release_render(void)
{
    scope_mutex_lock(&mutex);blocked=0;scope_condition_signal(&wake);scope_mutex_unlock(&mutex);
}

static void pin(const void *data,void *context)
{ (void)data;(void)context;scope_mutex_lock(&mutex);++pins;scope_mutex_unlock(&mutex); }
static void unpin(const void *data,void *context)
{ (void)data;(void)context;scope_mutex_lock(&mutex);assert(pins>0);--pins;scope_mutex_unlock(&mutex); }

static int gated_output(const uint32_t *pixels,void *context)
{
    int result;(void)pixels;(void)context;
    scope_mutex_lock(&mutex);entered=1;scope_condition_signal(&wake);
    while(blocked)scope_condition_wait(&wake,&mutex,100);
    result=output_result;scope_mutex_unlock(&mutex);return result;
}

int main(void)
{
    ScopeRenderer *r;ScopeRenderRegion region={&demo,sizeof(demo)};
    uint64_t generation,started,frames,ns;
    scope_mutex_init(&mutex);scope_condition_init(&wake);
    demo_signal_init(&demo);
    demo.screen.ch2_enabled=0;demo.screen.ch1_samples=borrowed;
    demo.screen.ch1_min_samples=demo.screen.ch1_max_samples=NULL;
    demo.screen.density_source[0].samples=0;demo.screen.history_count=0;
    for(unsigned i=0;i<SCOPE_PLOT_WIDTH;++i)borrowed[i]=250;
    scope_screen_render(expected,SCOPE_WIDTH,&demo.screen);
    r=scope_renderer_create(sizeof(demo),gated_render);assert(r);
    ScopeRenderRegion regions[]={region,{borrowed,sizeof(borrowed),pin,unpin,NULL}};
    blocked=1;generation=scope_renderer_submit(r,&demo.screen,regions,2);assert(generation);
    wait_entered();
    assert(pins==1);
    memset(&demo,0,sizeof(demo)); /* No borrowed model, strings or sample pointers. */
    release_render();assert(scope_renderer_flush(r,generation,actual,2000));
    assert(!memcmp(expected,actual,sizeof(expected)));
    assert(pins==0);

    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    scope_mutex_lock(&mutex);blocked=1;entered=0;scope_mutex_unlock(&mutex);
    assert(scope_renderer_submit(r,&demo.screen,&region,1));wait_entered();
    started=scope_clock_ns();
    for(unsigned i=0;i<100;++i){
        demo_signal_rotate(&demo,DEMO_ENC_TRIGGER,1);
        generation=scope_renderer_submit(r,&demo.screen,&region,1);assert(generation);
    }
    /* The painter is deliberately blocked, yet all controls and submissions
       completed. This checks independence, not a target-device latency limit. */
    assert(scope_clock_ns()-started<2000000000ULL);
    scope_screen_render(expected,SCOPE_WIDTH,&demo.screen);
    release_render();assert(scope_renderer_flush(r,generation,actual,2000));
    assert(!memcmp(expected,actual,sizeof(expected)));
    scope_renderer_metrics(r,&frames,&ns,NULL);assert(frames==3 && ns>0);
    scope_renderer_destroy(r);
    r=scope_renderer_create(sizeof(demo),NULL);assert(r);
    scope_renderer_set_output(r,gated_output,NULL);
    blocked=1;entered=0;assert(scope_renderer_submit(r,&demo.screen,&region,1));wait_entered();
    started=scope_clock_ns();
    for(unsigned i=0;i<100;++i){demo_signal_rotate(&demo,DEMO_ENC_TRIGGER,-1);
        generation=scope_renderer_submit(r,&demo.screen,&region,1);assert(generation);}
    assert(scope_clock_ns()-started<2000000000ULL);
    scope_mutex_lock(&mutex);output_result=-1;scope_mutex_unlock(&mutex);
    release_render();assert(scope_renderer_flush(r,generation,actual,2000));
    assert(scope_renderer_failed(r));scope_renderer_destroy(r);
    /* A blocked channel painter must not delay menu presentation. */
    demo_signal_init(&demo);demo_signal_begin_external(&demo);
    demo.screen.plot_id=41;
    ScopeScreen plot=demo.screen;plot.plot_only=1;
    scope_screen_render(plot_expected,SCOPE_WIDTH,&plot);
    blocked=0;output_result=1;
    r=scope_renderer_create(sizeof(demo),NULL);assert(r);
    assert(scope_renderer_enable_plot_layer(r,gated_plot));
    assert(scope_renderer_submit(r,&demo.screen,&region,1));
    assert(scope_renderer_flush_plot(r,2000));
    scope_mutex_lock(&mutex);blocked=1;entered=0;scope_mutex_unlock(&mutex);
    demo.screen.plot_id=42;
    demo_signal_ui_open_menu(&demo,DEMO_MENU_MAIN);
    generation=scope_renderer_submit(r,&demo.screen,&region,1);assert(generation);wait_entered();
    for(unsigned i=0;i<100;++i){demo_signal_rotate(&demo,DEMO_ENC_FUNCTION,i%2?1:-1);
        generation=scope_renderer_submit_overlay(r,&demo.screen,&region,1);assert(generation);}
    ScopeScreen ui=demo.screen;ui.plot_pixels=plot_expected;
    scope_screen_render(expected,SCOPE_WIDTH,&ui);
    assert(scope_renderer_flush(r,generation,actual,2000));
    assert(!memcmp(expected,actual,sizeof(expected)));
    assert(scope_renderer_presented_plot(r)==0);
    scope_renderer_acknowledge(r);assert(scope_renderer_presented_plot(r)==41);
    release_render();assert(scope_renderer_flush_plot(r,2000));
    generation=scope_renderer_submit_overlay(r,&demo.screen,&region,1);
    assert(scope_renderer_flush(r,generation,actual,2000));
    assert(scope_renderer_presented_plot(r)==41);
    scope_renderer_acknowledge(r);assert(scope_renderer_presented_plot(r)==42);
    scope_renderer_destroy(r);
    /* A slow UI painter can pin an older plot while pending snapshots and the
       channel painter advance. The final stopped refresh must still arrive. */
    blocked=0;demo.screen.plot_id=100;
    r=scope_renderer_create(sizeof(demo),gated_render);assert(r);
    assert(scope_renderer_enable_plot_layer(r,NULL));
    generation=scope_renderer_submit(r,&demo.screen,&region,1);assert(generation);
    assert(scope_renderer_flush_plot(r,2000));
    assert(scope_renderer_flush(r,generation,actual,2000));
    scope_mutex_lock(&mutex);blocked=1;entered=0;scope_mutex_unlock(&mutex);
    assert(scope_renderer_submit_overlay(r,&demo.screen,&region,1));wait_entered();
    for(unsigned i=101;i<=110;++i){
        demo.screen.plot_id=i;assert(scope_renderer_submit(r,&demo.screen,&region,1));
        assert(scope_renderer_flush_plot(r,2000));
    }
    generation=scope_renderer_submit_overlay(r,&demo.screen,&region,1);assert(generation);
    release_render();assert(scope_renderer_flush(r,generation,actual,2000));
    scope_renderer_acknowledge(r);assert(scope_renderer_presented_plot(r)==110);
    scope_renderer_destroy(r);
    scope_condition_destroy(&wake);scope_mutex_destroy(&mutex);
    return 0;
}
