#include "scope_renderer.h"
#include "scope_thread.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    ScopeScreen screen;
    unsigned char *snapshot;
    uint32_t *pixels;
    uint64_t generation;
    ScopeRenderRegion leases[5];
    unsigned lease_count;
    unsigned pins;
} RenderSlot;
struct ScopeRenderer {
    ScopeMutex mutex;
    ScopeCondition wake;
    ScopeThread thread;
    RenderSlot slots[5];
    unsigned slot_count;
    size_t capacity;
    unsigned pending,working,ready,reading;
    uint64_t generation,frames,nanoseconds,output_nanoseconds;
    int stop,failed;
    ScopeRenderFunction draw;
    ScopeRenderOutput output;
    void *output_context;
    ScopeRenderer *plot;
    const uint32_t *plot_pixels;
    uint64_t plot_generation,plot_ready_generation;
    uint64_t plot_ready_id,presented_id,presented_at_ns,presented_count,taken_id;
};

static const void *relocate(const void *pointer,const ScopeRenderRegion *regions,
                            const size_t *offsets,unsigned count,unsigned char *copy)
{
    for(unsigned i=0;i<count;++i){
        if(regions[i].retain)continue;
        uintptr_t offset=(uintptr_t)pointer-(uintptr_t)regions[i].data;
        if(offset<regions[i].bytes)return copy+offsets[i]+offset;
    }
    return pointer;
}

static void release_leases(RenderSlot *slot)
{
    for(unsigned i=0;i<slot->lease_count;++i){
        ScopeRenderRegion *lease=&slot->leases[i];lease->release(lease->data,lease->context);
    }
    slot->lease_count=0;
}

static void snapshot_screen(RenderSlot *slot,const ScopeScreen *screen,
                             const ScopeRenderRegion *regions,const size_t *offsets,unsigned count)
{
    ScopeScreen *s=&slot->screen;*s=*screen;
#define MOVE(field) s->field=relocate(s->field,regions,offsets,count,slot->snapshot)
    MOVE(plot_pixels);MOVE(response_curve);MOVE(ch1_samples);MOVE(ch2_samples);
    MOVE(ch1_min_samples);MOVE(ch1_max_samples);MOVE(ch2_min_samples);MOVE(ch2_max_samples);
    MOVE(capture_ch1_samples);MOVE(capture_ch2_samples);
    MOVE(history_ch1_samples);MOVE(history_ch2_samples);
    MOVE(history_ch1_min_samples);MOVE(history_ch1_max_samples);MOVE(history_ch2_min_samples);MOVE(history_ch2_max_samples);
    MOVE(ch1_scale);MOVE(ch2_scale);MOVE(ch1_input);MOVE(ch2_input);MOVE(time_scale);MOVE(zoom_label);
    MOVE(horizontal_position);MOVE(trigger_mode);MOVE(trigger_source);MOVE(trigger_level);MOVE(trigger_preview_level);
    MOVE(status_message);MOVE(browser_title);MOVE(browser_pixels);
    MOVE(fft_ch1_bins);MOVE(fft_ch2_bins);MOVE(fft_sampling_info);MOVE(fft_cursor_value);MOVE(menu_title);MOVE(menu_page);
    for(unsigned i=0;i<2;++i){MOVE(density_levels[i]);MOVE(density_source[i].context);
        MOVE(peak_min_samples[i]);MOVE(peak_max_samples[i]);MOVE(peak_strength[i]);MOVE(connections[i]);}
    for(unsigned i=0;i<SCOPE_MEASURE_SLOTS;++i){MOVE(measurement_labels[i]);MOVE(measurement_values[i]);}
    for(unsigned i=0;i<SCOPE_CURSOR_READOUTS;++i){MOVE(cursor_measurement_labels[i]);
        for(unsigned row=0;row<SCOPE_CURSOR_ROWS;++row){MOVE(cursor_measurement_values[row][i]);}}
    for(unsigned i=0;i<SCOPE_FFT_TICKS;++i){MOVE(fft_frequency_labels[i]);}
    for(unsigned i=0;i<SCOPE_MENU_ITEMS;++i){MOVE(menu_labels[i]);MOVE(menu_values[i]);MOVE(menu_options[i]);}
#undef MOVE
}

static void render_worker(void *context)
{
    ScopeRenderer *r=context;
    for(;;){
        unsigned slot;uint64_t started,drawn;int presented=0;
        scope_mutex_lock(&r->mutex);
        while(!r->pending && !r->stop)scope_condition_wait(&r->wake,&r->mutex,100);
        if(r->stop){scope_mutex_unlock(&r->mutex);return;}
        slot=r->pending;r->pending=0;r->working=slot;
        scope_mutex_unlock(&r->mutex);
        started=scope_clock_ns();
        r->draw(r->slots[slot-1].pixels,SCOPE_WIDTH,&r->slots[slot-1].screen);
        release_leases(&r->slots[slot-1]);
        drawn=scope_clock_ns();
        if(r->output){
            int result;
            while((result=r->output(r->slots[slot-1].pixels,r->output_context))==0){
                int superseded;
                scope_mutex_lock(&r->mutex);superseded=r->stop || r->pending;scope_mutex_unlock(&r->mutex);
                if(superseded)break;
                scope_sleep_ms(2);
            }
            if(result<0){scope_mutex_lock(&r->mutex);r->failed=1;scope_mutex_unlock(&r->mutex);}
            presented=result>0;
        }
        scope_mutex_lock(&r->mutex);
        uint64_t finished=scope_clock_ns();
        r->nanoseconds+=finished-started;r->output_nanoseconds+=finished-drawn;++r->frames;
        if(presented && r->presented_id!=r->slots[slot-1].screen.plot_id){
            r->presented_id=r->slots[slot-1].screen.plot_id;r->presented_at_ns=finished;++r->presented_count;
        }
        r->working=0;r->ready=slot;
        scope_mutex_unlock(&r->mutex);
    }
}

void scope_renderer_set_output(ScopeRenderer *r,ScopeRenderOutput output,void *context)
{ r->output=output;r->output_context=context; }

int scope_renderer_failed(ScopeRenderer *r)
{
    int failed;scope_mutex_lock(&r->mutex);failed=r->failed;scope_mutex_unlock(&r->mutex);
    return failed || (r->plot && scope_renderer_failed(r->plot));
}

static ScopeRenderer *create_renderer(size_t capacity,ScopeRenderFunction draw,unsigned slots)
{
    ScopeRenderer *r=calloc(1,sizeof(*r));
    if(!r)return NULL;
    r->capacity=capacity;r->draw=draw?draw:scope_screen_render;r->slot_count=slots;
    for(unsigned i=0;i<r->slot_count;++i){
        r->slots[i].snapshot=malloc(capacity?capacity:1);
        r->slots[i].pixels=calloc(SCOPE_WIDTH*SCOPE_HEIGHT,sizeof(uint32_t));
        if(!r->slots[i].snapshot || !r->slots[i].pixels)goto failed;
    }
    scope_mutex_init(&r->mutex);scope_condition_init(&r->wake);
    if(scope_thread_start(&r->thread,render_worker,r))return r;
    scope_condition_destroy(&r->wake);scope_mutex_destroy(&r->mutex);
failed:
    for(unsigned i=0;i<r->slot_count;++i){free(r->slots[i].snapshot);free(r->slots[i].pixels);}
    free(r);return NULL;
}

ScopeRenderer *scope_renderer_create(size_t capacity,ScopeRenderFunction draw)
{ return create_renderer(capacity,draw,3); }

static uint64_t submit(ScopeRenderer *r,const ScopeScreen *screen,
                       const ScopeRenderRegion *regions,unsigned count)
{
    size_t offsets[5],total=0;unsigned slot;uint64_t generation;
    if(!r || !screen || count>5)return 0;
    for(unsigned i=0;i<count;++i){
        offsets[i]=total;
        if(regions[i].retain){if(!regions[i].release || !regions[i].data)return 0;continue;}
        if(regions[i].bytes>r->capacity-total || (regions[i].bytes && !regions[i].data))return 0;
        total+=regions[i].bytes;
    }
    scope_mutex_lock(&r->mutex);
    slot=r->pending;r->pending=0;
    if(!slot)for(unsigned i=1;i<=r->slot_count;++i)if(i!=r->working && i!=r->ready && i!=r->reading && !r->slots[i-1].pins){slot=i;break;}
    if(!slot && r->ready && r->ready!=r->reading && !r->slots[r->ready-1].pins){slot=r->ready;r->ready=0;}
    generation=slot?++r->generation:0;
    scope_mutex_unlock(&r->mutex);
    if(!slot)return 0;
    release_leases(&r->slots[slot-1]);
    for(unsigned i=0;i<count;++i){
        if(regions[i].retain){regions[i].retain(regions[i].data,regions[i].context);
            r->slots[slot-1].leases[r->slots[slot-1].lease_count++]=regions[i];}
        else if(regions[i].bytes)memcpy(r->slots[slot-1].snapshot+offsets[i],regions[i].data,regions[i].bytes);
    }
    snapshot_screen(&r->slots[slot-1],screen,regions,offsets,count);
    r->slots[slot-1].generation=generation;
    scope_mutex_lock(&r->mutex);r->pending=slot;scope_condition_signal(&r->wake);scope_mutex_unlock(&r->mutex);
    return generation;
}

static void draw_plot(uint32_t *pixels,int stride,const ScopeScreen *screen)
{
    ScopeScreen plot=*screen;plot.plot_only=1;plot.plot_pixels=NULL;
    scope_screen_render(pixels,stride,&plot);
}

int scope_renderer_enable_plot_layer(ScopeRenderer *r,ScopeRenderFunction draw)
{
    if(r->plot)return 1;
    /* UI may pin a working and a pending plot while the newest cached plot is
       leased. Two more slots let the channel painter work and queue a refresh. */
    r->plot=create_renderer(r->capacity,draw?draw:draw_plot,5);
    return r->plot!=NULL;
}

static void pin_plot(const void *pixels,void *context)
{
    ScopeRenderer *r=context;scope_mutex_lock(&r->mutex);
    for(unsigned i=0;i<r->slot_count;++i)if(r->slots[i].pixels==pixels){++r->slots[i].pins;break;}
    scope_mutex_unlock(&r->mutex);
}
static void unpin_plot(const void *pixels,void *context)
{
    ScopeRenderer *r=context;scope_mutex_lock(&r->mutex);
    for(unsigned i=0;i<r->slot_count;++i)if(r->slots[i].pixels==pixels){--r->slots[i].pins;break;}
    scope_mutex_unlock(&r->mutex);
}

int scope_renderer_poll_plot(ScopeRenderer *r)
{
    ScopeRenderer *p=r->plot;unsigned slot;
    if(!p)return 0;
    scope_mutex_lock(&p->mutex);slot=p->ready;
    if(slot){p->ready=0;p->reading=slot;
        r->plot_pixels=p->slots[slot-1].pixels;r->plot_ready_generation=p->slots[slot-1].generation;}
    if(slot)r->plot_ready_id=p->slots[slot-1].screen.plot_id;
    scope_mutex_unlock(&p->mutex);
    return slot!=0;
}

int scope_renderer_flush_plot(ScopeRenderer *r,unsigned timeout)
{
    uint64_t deadline=scope_clock_ns()/1000000+timeout;
    if(!r->plot)return 1;
    do{scope_renderer_poll_plot(r);
        if(r->plot_ready_generation>=r->plot_generation)return 1;
        scope_sleep_ms(1);
    }while(scope_clock_ns()/1000000<deadline);
    return 0;
}

static uint64_t submit_layers(ScopeRenderer *r,const ScopeScreen *screen,
                               const ScopeRenderRegion *regions,unsigned count,int update_plot)
{
    ScopeScreen ui;ScopeRenderRegion copied[5];uint64_t generation;
    if(!r || !r->plot)return submit(r,screen,regions,count);
    if(!screen || count>4)return 0;
    if(update_plot){generation=submit(r->plot,screen,regions,count);
        if(generation)r->plot_generation=generation;}
    ui=*screen;ui.plot_pixels=r->plot_pixels;ui.plot_id=r->plot_ready_id;
    /* Before the first plot completes, draw an empty plot rather than making
       the controls wait for channel rendering during startup. */
    if(!ui.plot_pixels){ui.ch1_samples=ui.ch2_samples=NULL;
        ui.ch1_min_samples=ui.ch1_max_samples=ui.ch2_min_samples=ui.ch2_max_samples=NULL;
        ui.history_count=0;ui.peak_detect=0;
        for(unsigned i=0;i<2;++i){ui.density_levels[i]=NULL;ui.connections[i]=NULL;ui.density_source[i].samples=0;}}
    memcpy(copied,regions,count*sizeof(*regions));
    if(ui.plot_pixels)copied[count++]=(ScopeRenderRegion){ui.plot_pixels,0,pin_plot,unpin_plot,r->plot};
    return submit(r,&ui,copied,count);
}

uint64_t scope_renderer_submit(ScopeRenderer *r,const ScopeScreen *screen,
                               const ScopeRenderRegion *regions,unsigned count)
{ return submit_layers(r,screen,regions,count,1); }

uint64_t scope_renderer_submit_plot(ScopeRenderer *r,const ScopeScreen *screen,
                                    const ScopeRenderRegion *regions,unsigned count)
{
    if(!r || !r->plot)return submit(r,screen,regions,count);
    uint64_t generation=submit(r->plot,screen,regions,count);
    if(generation)r->plot_generation=generation;
    return generation;
}

uint64_t scope_renderer_submit_overlay(ScopeRenderer *r,const ScopeScreen *screen,
                                       const ScopeRenderRegion *regions,unsigned count)
{ return submit_layers(r,screen,regions,count,0); }

uint64_t scope_renderer_take(ScopeRenderer *r,uint32_t *pixels)
{
    unsigned slot;uint64_t generation;
    if(!r || !pixels)return 0;
    scope_mutex_lock(&r->mutex);slot=r->ready;
    if(slot){r->ready=0;r->reading=slot;}
    scope_mutex_unlock(&r->mutex);
    if(!slot)return 0;
    memcpy(pixels,r->slots[slot-1].pixels,SCOPE_WIDTH*SCOPE_HEIGHT*sizeof(*pixels));
    generation=r->slots[slot-1].generation;
    scope_mutex_lock(&r->mutex);r->taken_id=r->slots[slot-1].screen.plot_id;r->reading=0;scope_mutex_unlock(&r->mutex);
    return generation;
}

uint64_t scope_renderer_presented_plot(ScopeRenderer *r)
{
    uint64_t id;scope_mutex_lock(&r->mutex);id=r->presented_id;scope_mutex_unlock(&r->mutex);return id;
}
uint64_t scope_renderer_presented_time(ScopeRenderer *r)
{
    uint64_t at;scope_renderer_presentation(r,NULL,&at);return at;
}
void scope_renderer_presentation(ScopeRenderer *r,uint64_t *count,uint64_t *at)
{
    scope_mutex_lock(&r->mutex);
    if(count)*count=r->presented_count;
    if(at)*at=r->presented_at_ns;
    scope_mutex_unlock(&r->mutex);
}
void scope_renderer_acknowledge(ScopeRenderer *r)
{
    scope_mutex_lock(&r->mutex);
    if(r->presented_id!=r->taken_id){r->presented_id=r->taken_id;r->presented_at_ns=scope_clock_ns();++r->presented_count;}
    scope_mutex_unlock(&r->mutex);
}

int scope_renderer_flush(ScopeRenderer *r,uint64_t generation,uint32_t *pixels,unsigned timeout)
{
    uint64_t deadline=scope_clock_ns()/1000000+timeout;
    if(!generation)return 0;
    do{if(scope_renderer_take(r,pixels)>=generation)return 1;scope_sleep_ms(1);}while(scope_clock_ns()/1000000<deadline);
    return 0;
}

void scope_renderer_metrics(ScopeRenderer *r,uint64_t *frames,uint64_t *nanoseconds,uint64_t *output_nanoseconds)
{
    scope_mutex_lock(&r->mutex);*frames=r->frames;*nanoseconds=r->nanoseconds;
    if(output_nanoseconds)*output_nanoseconds=r->output_nanoseconds;
    scope_mutex_unlock(&r->mutex);
}

void scope_renderer_destroy(ScopeRenderer *r)
{
    if(!r)return;
    scope_mutex_lock(&r->mutex);r->stop=1;scope_condition_signal(&r->wake);scope_mutex_unlock(&r->mutex);
    scope_thread_join(&r->thread);scope_condition_destroy(&r->wake);scope_mutex_destroy(&r->mutex);
    for(unsigned i=0;i<r->slot_count;++i){release_leases(&r->slots[i]);free(r->slots[i].snapshot);free(r->slots[i].pixels);}
    scope_renderer_destroy(r->plot);
    free(r);
}
