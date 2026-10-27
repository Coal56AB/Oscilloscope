#include "panel_sdl.h"
#include "wave_file.h"
#include "linux_storage.h"
#include "linux_serial.h"
#include "capture_adapter.h"
#include "settings_session.h"
#include "scope_renderer.h"
#include "waveform_view.h"
#include "control_transport.h"
#include "remote_control.h"
#include "status_ipc.h"
#ifdef SCOPE_ENABLE_ZYNQ
#include "capture_selftest.h"
#endif
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
#include "linux_display.h"
#include "linux_touch.h"
#endif

#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static DemoSignal demo;
static SettingsSession settings;
static ScopeRenderer *renderer;
static uint64_t render_generation;
static int restored_scale[2]={-1,-1};
static int panel_channel_enabled[2]={-1,-1};
static uint64_t restore_retry_ms;
static int restore_capture_pending;
static volatile sig_atomic_t stop_requested;
static void request_stop(int signal_number) { (void)signal_number; stop_requested=1; }
static uint32_t *screen_pixels;
static uint32_t *panel_pixels;
static uint32_t browse_pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
static DemoWaveCapture browse_wave;
static DemoSignal live_before_browse;
static int live_snapshot_valid;
static char browse_names[DEMO_BROWSE_FILES][DEMO_BROWSE_NAME];
static char application_dir[PATH_MAX];
static int require_data_device;
static int dirty = 1,plot_dirty=1,overlay_dirty=1;
static int panel_enabled = 1;
static ControlQueue controls;
static KeyboardControlTransport keyboard_controls;
static SerialControlTransport serial_controls;
static CapturePipeline *capture_pipeline;
static DisplayFrame capture_frame;
static const DisplayFrame *leased_frame;
static int capture_frame_valid;
static uint64_t single_capture_id;
static const char *console_path;
static unsigned console_baud = 115200;
static char startup_error[64];
static uint64_t pc_activity_until;

static int request_panel_scale(void *context, unsigned channel, int millivolts)
{
    int queued=serial_control_set_scale(context,channel,millivolts);
    if(queued){restored_scale[channel]=millivolts;restore_retry_ms=0;}
    return queued;
}

static void update_pc_console(uint64_t now)
{
    unsigned requested = demo_signal_pc_baud(demo.pc_baud_index);
    int active = now < pc_activity_until;
    if (requested != console_baud) {
        int ok = console_path && linux_serial_set_baud(console_path, requested);
        if (ok) console_baud = requested;
        demo_signal_pc_status(&demo, demo.pc_available, console_baud, active);
        demo_signal_notify(&demo, ok ? "UART BAUD APPLIED" : "UART BAUD FAILED");
        dirty = 1;
    } else if (demo.pc_active != active) {
        demo_signal_pc_status(&demo, demo.pc_available, console_baud, active);
        dirty = 1;
    }
}

typedef struct {
    DemoControl control;
    SDL_Keycode key_code;
    uint64_t since;
    int active, long_done, suppressed;
} HeldControl;

static HeldControl mouse_hold;
static HeldControl key_holds[CONTROL_ID_COUNT];
static DemoControl hovered = DEMO_CONTROL_COUNT;
static DemoControl pending_chord = DEMO_CONTROL_COUNT;
static uint64_t pending_time;
static DemoSignal chord_snapshot;
static int wheel_remainder;


typedef enum {
    TOUCH_NONE, TOUCH_TOP_MENU, TOUCH_TOP_SCREENSHOT, TOUCH_TOP_WAVE,
    TOUCH_TOP_FINE, TOUCH_TOP_PROCESSING, TOUCH_TOP_RUN,
    TOUCH_CH1, TOUCH_CH2, TOUCH_TIME, TOUCH_TRIGGER_SOURCE,
    TOUCH_BOTTOM_CURSOR, TOUCH_MENU_BACK, TOUCH_MENU_DRAG, TOUCH_MENU_OUTSIDE, TOUCH_MENU_ROW,
    TOUCH_MEASURE_HIDE, TOUCH_MEASURE_CLEAR, TOUCH_MEASURE_CANCEL,
    TOUCH_MEASURE_CONFIRM, TOUCH_PLOT_TRIGGER, TOUCH_PLOT_CURSOR,
    TOUCH_BROWSE_DELETE_CANCEL, TOUCH_BROWSE_DELETE_CONFIRM,
    TOUCH_MEASURE_DRAG, TOUCH_MEASURE_LAYOUT, TOUCH_CURSOR_STRIP,
    TOUCH_ZOOM_OVERVIEW, TOUCH_PLOT_CH1, TOUCH_PLOT_CH2,
    TOUCH_TIME_IN, TOUCH_TIME_OUT, TOUCH_BROWSER_PREV, TOUCH_BROWSER_NEXT,
    TOUCH_BROWSER_BACK, TOUCH_SPLIT, TOUCH_FFT_CURSOR
} TouchZone;

typedef struct {
    TouchZone zone;
    int row, x, y, last_x, last_y, remainder;
    int fine_x_remainder, fine_y_remainder;
    int axis, active, moved, long_done;
    uint64_t since;
} TouchInput;

typedef struct { SDL_FingerID id; int x, y, active; } TouchContact;
static TouchInput touch;
static TouchContact contacts[2];
static uint64_t pinch_start_sq;
static int pinch_consumed, pinch_block;

#define CHORD_WINDOW_MS 90

static uint64_t now_ms(void)
{
    static uint32_t previous;
    static uint64_t high;
    uint32_t current = SDL_GetTicks();
    if (current < previous) high += UINT64_C(1) << 32;
    previous = current;
    return high + current;
}
static void invalidate(void) { dirty = plot_dirty = overlay_dirty = 1; }

static int capture_waiting(const CaptureMetrics *metrics,uint64_t presented,int running,int mode)
{
    return running && mode!=0 && metrics->triggered_ns<=presented;
}

static int has_hold_action(DemoControl control)
{
    return control != DEMO_CONTROL_COUNT;
}

static DemoControl active_control(int *progress)
{
    const HeldControl *held = mouse_hold.active ? &mouse_hold : NULL;int i;
    for(i=0;i<CONTROL_ID_COUNT&&!held;++i)if(key_holds[i].active)held=&key_holds[i];
    *progress = -1;
    if (!held) return DEMO_CONTROL_COUNT;
    if (!held->suppressed && has_hold_action(held->control)) {
        uint64_t elapsed = now_ms() - held->since;
        *progress = held->long_done || elapsed >= (uint64_t)demo.hold_time_ms ? 100 :
                    (int)(elapsed * 100 / (uint64_t)demo.hold_time_ms);
    }
    return held->control;
}

static void retain_display_frame(const void *frame,void *context)
{ capture_pipeline_retain_frame(context,frame); }
static void release_display_frame(const void *frame,void *context)
{ capture_pipeline_release_frame(context,frame); }

#ifdef SCOPE_ENABLE_LINUX_DISPLAY
static int present_display(const uint32_t *pixels,void *context)
{ return linux_display_present(context,pixels); }
#endif

static void draw_all(void)
{
    int progress;
    const DisplayFrame *frame=leased_frame?leased_frame:&capture_frame;
    DemoControl active = active_control(&progress);
    if(capture_pipeline&&capture_frame_valid&&!demo.waveform_loaded&&!demo.screen.browser_visible)
        capture_display_apply(&demo,frame);
    else {
        demo.screen.ch1_min_samples=demo.minimum[0];
        demo.screen.ch1_max_samples=demo.maximum[0];
        demo.screen.ch2_min_samples=demo.minimum[1];
        demo.screen.ch2_max_samples=demo.maximum[1];
    }
    if(renderer){
        ScopeRenderRegion regions[]={{&demo,sizeof(demo)},{frame,0,capture_frame_valid && leased_frame?retain_display_frame:NULL,
             capture_frame_valid && leased_frame?release_display_frame:NULL,capture_pipeline},
            {browse_pixels,demo.screen.browser_visible?sizeof(browse_pixels):0}};
        if(plot_dirty && !overlay_dirty)
            scope_renderer_submit_plot(renderer,&demo.screen,regions,3);
        else render_generation=plot_dirty?scope_renderer_submit(renderer,&demo.screen,regions,3):
            scope_renderer_submit_overlay(renderer,&demo.screen,regions,3);
        plot_dirty=overlay_dirty=0;
    }else scope_screen_render(screen_pixels, SCOPE_WIDTH, &demo.screen);
    if(panel_enabled)
        panel_sdl_draw(panel_pixels, &demo, active, hovered, progress, demo.hold_time_ms);
}

static void put_u16(FILE *file, uint16_t value)
{
    fputc(value & 255, file); fputc(value >> 8, file);
}

static void put_u32(FILE *file, uint32_t value)
{
    put_u16(file, (uint16_t)value); put_u16(file, (uint16_t)(value >> 16));
}

static uint16_t get_u16(FILE *file)
{
    int a=fgetc(file),b=fgetc(file); return (uint16_t)(a | (b << 8));
}

static uint32_t get_u32(FILE *file)
{
    uint32_t a=get_u16(file),b=get_u16(file); return a | (b << 16);
}

static int save_bmp(const char *path, const uint32_t *pixels, int width, int height)
{
    FILE *file=fopen(path,"wb"); int y;
    uint32_t offset=14+40, size=offset+(uint32_t)width*(uint32_t)height*4;
    if(!file) return 0;
    put_u16(file,0x4d42); put_u32(file,size); put_u16(file,0); put_u16(file,0); put_u32(file,offset);
    put_u32(file,40); put_u32(file,(uint32_t)width); put_u32(file,(uint32_t)height);
    put_u16(file,1); put_u16(file,32); put_u32(file,0); put_u32(file,size-offset);
    put_u32(file,2835); put_u32(file,2835); put_u32(file,0); put_u32(file,0);
    for(y=height-1;y>=0;--y)
        if(fwrite(pixels+(size_t)y*width,4,(size_t)width,file)!=(size_t)width) {
            fclose(file); return 0;
        }
    return fclose(file)==0;
}

static int save_wave(const char *path)
{
    FILE *file=fopen(path,"w"); DemoWaveCapture wave; int ok;
    if(!file) return 0;
    demo_signal_export_wave(&demo,&wave); ok=wave_file_write(file,&wave);
    return fclose(file)==0 && ok;
}

static int make_path(char *path, size_t size, const char *name)
{
    int written=snprintf(path,size,"%s/%s",application_dir,name);
    return written>=0 && (size_t)written<size;
}

static int compare_browse_names(const void *left, const void *right)
{
    return strcmp((const char *)right,(const char *)left);
}

static int has_extension(const char *name, const char *extension)
{
    size_t nl=strlen(name),el=strlen(extension);
    return nl>=el && strcasecmp(name+nl-el,extension)==0;
}

static void refresh_browse(void)
{
    DIR *directory=opendir(application_dir); struct dirent *entry;
    const char *names[DEMO_BROWSE_FILES]; int count=0,i;
    const char *extension=demo.browse_filter?".csv":".bmp";
    if(directory) {
        while((entry=readdir(directory))!=NULL && count<DEMO_BROWSE_FILES) {
            if(strncmp(entry->d_name,"capture_",8)==0 &&
               has_extension(entry->d_name,extension) &&
               strlen(entry->d_name)<DEMO_BROWSE_NAME) {
                memcpy(browse_names[count],entry->d_name,strlen(entry->d_name)+1);
                ++count;
            }
        }
        closedir(directory);
    }
    qsort(browse_names,(size_t)count,sizeof(browse_names[0]),compare_browse_names);
    for(i=0;i<count;++i) names[i]=browse_names[i];
    demo_signal_ui_set_browse_files(&demo,names,count);
}

static int delete_browse_capture(void)
{
    const char *name=demo_signal_ui_browse_filename(&demo);char path[PATH_MAX];
    return name&&make_path(path,sizeof(path),name)&&remove(path)==0;
}

static int load_browse_bmp(const char *path)
{
    FILE *file=fopen(path,"rb"); uint32_t offset,header,width,height,compression; uint16_t bits; int y;
    if(!file) return 0;
    if(get_u16(file)!=0x4d42) { fclose(file); return 0; }
    (void)get_u32(file); (void)get_u16(file); (void)get_u16(file); offset=get_u32(file);
    header=get_u32(file); width=get_u32(file); height=get_u32(file);
    (void)get_u16(file); bits=get_u16(file); compression=get_u32(file);
    if(header<40 || width!=SCOPE_WIDTH || height!=SCOPE_HEIGHT || bits!=32 || compression!=0 ||
       fseek(file,(long)offset,SEEK_SET)!=0) { fclose(file); return 0; }
    for(y=SCOPE_HEIGHT-1;y>=0;--y)
        if(fread(browse_pixels+(size_t)y*SCOPE_WIDTH,4,SCOPE_WIDTH,file)!=SCOPE_WIDTH) {
            fclose(file); return 0;
        }
    fclose(file); return 1;
}

static int load_browse_wave(const char *path)
{
    FILE *file=fopen(path,"r"); int ok;
    if(!file) return 0;
    demo_signal_export_wave(&demo,&browse_wave); ok=wave_file_read(file,&browse_wave);
    return fclose(file)==0 && ok;
}

static DemoSignal *live_settings(void)
{
    if(!live_snapshot_valid)return &demo;
    live_before_browse.startup_preset=demo.startup_preset;
    live_before_browse.preset_valid=demo.preset_valid;
    return &live_before_browse;
}

static int open_browse_capture(void)
{
    const char *name=demo_signal_ui_browse_filename(&demo); char path[PATH_MAX]; size_t length;
    if(!name || !make_path(path,sizeof(path),name)) return 0;
    length=strlen(name);
    if(length>=4 && strcasecmp(name+length-4,".bmp")==0) {
        if(!load_browse_bmp(path)) return 0;
        if(live_snapshot_valid) {
            int selected=demo.browse_selected; demo=*live_settings();
            demo.browse_selected=selected; live_snapshot_valid=0;
        }
        demo_signal_ui_show_capture(&demo,demo.browse_files[demo.browse_selected],browse_pixels);
    } else {
        if(!load_browse_wave(path)) return 0;
        if(live_snapshot_valid) {
            int selected=demo.browse_selected; demo=*live_settings();
            demo.browse_selected=selected; live_snapshot_valid=0;
        }
        live_before_browse=demo; live_snapshot_valid=1;
        demo_signal_ui_show_wave(&demo,demo.browse_files[demo.browse_selected],&browse_wave);
    }
    return 1;
}

static int capture(DemoAction action)
{
    char image_path[PATH_MAX],wave_path[PATH_MAX]; int index, ok=0;
    struct stat information;
    int directory=linux_storage_open(application_dir,require_data_device);
    if(directory<0) return require_data_device?-1:0;
    /* Resolve all files through this fd: an unmount cannot redirect them into RAM. */
    for(index=1;index<=9999;++index) {
        snprintf(image_path,sizeof(image_path),"/proc/self/fd/%d/capture_%04d.bmp",directory,index);
        snprintf(wave_path,sizeof(wave_path),"/proc/self/fd/%d/capture_%04d.csv",directory,index);
        if(stat(image_path,&information)!=0 && stat(wave_path,&information)!=0) break;
    }
    if(index<=9999) {
        ok=1;
        if(action!=DEMO_ACTION_WAVEFORM) {
            if(renderer){plot_dirty=1;draw_all();
                if(!scope_renderer_flush_plot(renderer,1000)){close(directory);return 0;}
                plot_dirty=0;draw_all();
                if(!scope_renderer_flush(renderer,render_generation,screen_pixels,1000)){close(directory);return 0;}}
            else scope_screen_render(screen_pixels,SCOPE_WIDTH,&demo.screen);
            ok=save_bmp(image_path,screen_pixels,SCOPE_WIDTH,SCOPE_HEIGHT);
        }
        if(ok && action!=DEMO_ACTION_SCREENSHOT) ok=save_wave(wave_path);
    }
    if(ok) return linux_storage_sync_close(directory);
    close(directory);
    return 0;
}

static void act(DemoAction action)
{
    if(action==DEMO_ACTION_PRESET_SAVE){
        unsigned slot=(unsigned)demo.preset_selected;
        int ok=settings_session_save(&settings,live_settings(),slot);
        demo.preset_valid=settings.document.valid;demo_signal_refresh(&demo);
        demo_signal_notify(&demo,ok?"SAVING PRESET":"PRESET SAVE FAILED");
    }else if(action==DEMO_ACTION_PRESET_LOAD){
        unsigned slot=(unsigned)demo.preset_selected;
        if(live_snapshot_valid)act(DEMO_ACTION_BROWSE_CLOSE);
        if(settings_session_load(&settings,&demo,slot)){
            unsigned ch;demo_signal_ui_dismiss_menu(&demo);
            if(serial_controls.started)for(ch=0;ch<2;++ch)restored_scale[ch]=demo_signal_panel_scale(&demo,ch);
            restore_retry_ms=0;
            demo_signal_notify(&demo,"PRESET LOADED");
        }else demo_signal_notify(&demo,"PRESET EMPTY");
    }else if(action==DEMO_ACTION_BROWSE_REFRESH) refresh_browse();
    else if(action==DEMO_ACTION_BROWSE_OPEN) {
        if(!open_browse_capture()) demo_signal_notify(&demo,"OPEN FAILED");
    } else if(action==DEMO_ACTION_BROWSE_PREV || action==DEMO_ACTION_BROWSE_NEXT) {
        if((demo.screen.browser_visible||demo.waveform_loaded)&&demo.browse_count) {
            int previous=demo.browse_selected;
            int direction=action==DEMO_ACTION_BROWSE_NEXT?1:-1;
            demo.browse_selected=(previous+direction+demo.browse_count)%demo.browse_count;
            if(!open_browse_capture()) { demo.browse_selected=previous; demo_signal_notify(&demo,"OPEN FAILED"); }
        }
    } else if(action==DEMO_ACTION_BROWSE_DELETE) {
        if(delete_browse_capture()) {refresh_browse();demo_signal_notify(&demo,"FILE DELETED");}
        else demo_signal_notify(&demo,"DELETE FAILED");
    } else if(action==DEMO_ACTION_BROWSE_CLOSE) {
        if(live_snapshot_valid) {
            int selected=demo.browse_selected;
            demo=*live_settings(); demo.browse_selected=selected; live_snapshot_valid=0;
            demo_signal_ui_close_capture(&demo);
        }
    } else if(action!=DEMO_ACTION_NONE) {
        int ok=capture(action);
        demo_signal_notify(&demo,ok==1?(action==DEMO_ACTION_SCREENSHOT?"SCREENSHOT SAVED":
            action==DEMO_ACTION_WAVEFORM?"WAVEFORM SAVED":"SCREEN AND WAVE SAVED"):ok<0?"NO DATA CARD":"SAVE FAILED");
    }
    invalidate();
}

static void press_control_at(DemoControl control, int long_press, uint64_t when)
{
    act(demo_signal_press_at(&demo,control,long_press,when));
    invalidate();
}

static void suppress_chord_control(DemoControl control)
{
    int i;
    if(mouse_hold.active && mouse_hold.control==control) mouse_hold.suppressed=1;
    for(i=0;i<CONTROL_ID_COUNT;++i) if(key_holds[i].active&&key_holds[i].control==control) key_holds[i].suppressed=1;
}

static void begin_press(HeldControl *held, DemoControl control, SDL_Keycode key_code, uint64_t now)
{
    held->control=control; held->key_code=key_code; held->since=now;
    held->active=1; held->long_done=0; held->suppressed=0;
    if(control==DEMO_BTN_CH1 || control==DEMO_BTN_MENU) {
        if(pending_chord!=DEMO_CONTROL_COUNT && pending_chord!=control &&
           now>=pending_time && now-pending_time<=CHORD_WINDOW_MS) {
            demo=chord_snapshot; suppress_chord_control(pending_chord); held->suppressed=1;
            pending_chord=DEMO_CONTROL_COUNT; act(DEMO_ACTION_SCREEN_AND_WAVE); return;
        }
        chord_snapshot=demo; pending_chord=control; pending_time=now;
    } else pending_chord=DEMO_CONTROL_COUNT;
}

static void finish_press(HeldControl *held, uint64_t now)
{
    if(!held->active) return;
    if(!held->suppressed&&!held->long_done)
        press_control_at(held->control,has_hold_action(held->control)&&
                         now-held->since>=(uint64_t)demo.hold_time_ms,now);
    held->active=0;
}

static void fire_long_press(HeldControl *held, uint64_t now)
{
    if(!held->active||held->suppressed||held->long_done||!has_hold_action(held->control)||
       now-held->since<(uint64_t)demo.hold_time_ms) return;
    held->long_done=1; pending_chord=DEMO_CONTROL_COUNT;
    press_control_at(held->control,1,now);
}

static int touch_screen_y(int logical_y)
{
    return scope_screen_waveform_top(&demo.screen)+
        logical_y*scope_screen_waveform_height(&demo.screen)/SCOPE_PLOT_HEIGHT;
}

static int touch_plot_y(int screen_y)
{
    return (screen_y-scope_screen_waveform_top(&demo.screen))*SCOPE_PLOT_HEIGHT/
        scope_screen_waveform_height(&demo.screen);
}

static int touch_channel_at(int x, int y, int tolerance)
{
    int sample=x-SCOPE_PLOT_X,d1,d2;
    if(sample<0) sample=0;
    if(sample>=SCOPE_PLOT_WIDTH) sample=SCOPE_PLOT_WIDTH-1;
    d1=demo.screen.ch1_enabled?abs(y-touch_screen_y(x<SCOPE_PLOT_X+40?
       demo.screen.channel_zero_y[0]:demo.ch1[sample])):10000;
    d2=demo.screen.ch2_enabled?abs(y-touch_screen_y(x<SCOPE_PLOT_X+40?
       demo.screen.channel_zero_y[1]:demo.ch2[sample])):10000;
    if(d1==10000&&d2==10000) return -1;
    if(tolerance>=0&&d1>tolerance&&d2>tolerance) return -1;
    return d1<=d2?0:1;
}

static TouchZone touch_zone_at(int x, int y, int *row)
{
    int menu_x=demo.screen.menu_x;
    int menu_y=demo.screen.menu_y;
    int menu_row_y=menu_y+(SCOPE_MENU_ROW_Y-SCOPE_MENU_Y);
    if(x<0||x>=SCOPE_WIDTH||y<0||y>=SCOPE_HEIGHT) return TOUCH_NONE;
    if(demo.screen.browser_visible) {
        if(y<SCOPE_BOTTOM_Y) return TOUCH_NONE;
        if(x>=12&&x<152) return TOUCH_BROWSER_PREV;
        if(x>=160&&x<300) return TOUCH_BROWSER_NEXT;
        return x>=872&&x<1012?TOUCH_BROWSER_BACK:TOUCH_NONE;
    }
    if(demo.screen.browser_delete_confirm) {
        int left=SCOPE_BROWSE_CONFIRM_X,top=SCOPE_BROWSE_CONFIRM_Y;
        if(y>=top+88&&y<top+132&&x>=left+216&&x<left+404)return TOUCH_BROWSE_DELETE_CONFIRM;
        return TOUCH_BROWSE_DELETE_CANCEL;
    }
    if(demo.screen.measurement_clear_confirm) {
        int left=SCOPE_MEASURE_CONFIRM_X,top=SCOPE_MEASURE_CONFIRM_Y;
        if(y>=top+88&&y<top+132&&x>=left+216&&x<left+404) return TOUCH_MEASURE_CONFIRM;
        return TOUCH_MEASURE_CANCEL;
    }
    if(demo.screen.measurement_menu&&x>=20&&x<1004&&y>=74&&y<SCOPE_BOTTOM_Y-10) {
        if(y<SCOPE_MEASURE_MENU_ROW_Y)
            return x>=902?TOUCH_MENU_BACK:x>=588&&x<728&&y>=83&&y<110?TOUCH_MEASURE_HIDE:
                   x>=744&&x<884&&y>=83&&y<110?TOUCH_MEASURE_CLEAR:TOUCH_NONE;
        *row=(x>=512?8:0)+(y-SCOPE_MEASURE_MENU_ROW_Y)/SCOPE_MEASURE_MENU_ROW_HEIGHT;
        return (y-SCOPE_MEASURE_MENU_ROW_Y)/SCOPE_MEASURE_MENU_ROW_HEIGHT<8&&
               *row<SCOPE_MEASURE_CATALOG_ITEMS?TOUCH_MENU_ROW:TOUCH_NONE;
    }
    if(demo.screen.menu_open&&x>=menu_x&&x<menu_x+SCOPE_MENU_WIDTH&&
       !demo.screen.measurement_menu&&y>=menu_y&&
       y<menu_row_y+demo.screen.menu_count*SCOPE_MENU_ROW_HEIGHT+6) {
        if(y<menu_row_y) return x>=menu_x+SCOPE_MENU_WIDTH-104?TOUCH_MENU_BACK:TOUCH_MENU_DRAG;
        *row=(y-menu_row_y)/SCOPE_MENU_ROW_HEIGHT;
        return *row<demo.screen.menu_count && !demo.screen.menu_disabled[*row]?TOUCH_MENU_ROW:TOUCH_NONE;
    }
    if(y<SCOPE_PLOT_Y) {
        if(x>=4&&x<184)return TOUCH_TOP_MENU;
        if(x>=188&&x<368)return TOUCH_TOP_SCREENSHOT;
        if(x>=372&&x<548)return TOUCH_TOP_WAVE;
        if(x>=552&&x<692)return TOUCH_TOP_FINE;
        if(x>=696&&x<852)return TOUCH_TOP_PROCESSING;
        if(x>=856&&x<1020)return TOUCH_TOP_RUN;
    }
    if(y>=SCOPE_BOTTOM_Y&&y<SCOPE_BOTTOM_Y+SCOPE_BOTTOM_HEIGHT) {
        if(demo.waveform_loaded) {
            if(x>=12&&x<152)return TOUCH_BROWSER_PREV;
            if(x>=160&&x<300)return TOUCH_BROWSER_NEXT;
            if(x>=306&&x<478)return TOUCH_CH1;
            if(x>=484&&x<656)return TOUCH_CH2;
            if(x>=662&&x<834)return TOUCH_TIME;
            return x>=840&&x<1018?TOUCH_BOTTOM_CURSOR:TOUCH_NONE;
        }
        if(x>=6&&x<200)return TOUCH_CH1;
        if(x>=206&&x<400)return TOUCH_CH2;
        if(x>=406&&x<600)return TOUCH_TIME;
        if(x>=606&&x<800)return TOUCH_TRIGGER_SOURCE;
        if(x>=806&&x<1018)return TOUCH_BOTTOM_CURSOR;
    }
    if(demo.screen.menu_open) return y>=SCOPE_PLOT_Y&&y<SCOPE_BOTTOM_Y?TOUCH_MENU_OUTSIDE:TOUCH_NONE;
    if(demo.screen.response_visible)return TOUCH_NONE;
    if(x>=SCOPE_PLOT_X&&x<SCOPE_PLOT_X+SCOPE_PLOT_WIDTH&&y>=SCOPE_PLOT_Y&&y<SCOPE_PLOT_Y+SCOPE_PLOT_HEIGHT) {
        int mx,my,mw,mh;
        int cursor_y,cursor_height;
        scope_screen_cursor_measurement_bounds(&demo.screen,NULL,&cursor_y,NULL,&cursor_height);
        if(demo.screen.cursor_measurement_count>0&&y>=cursor_y&&y<cursor_y+cursor_height&&
           x>=0&&x<scope_screen_cursor_measurement_width(&demo.screen)) return TOUCH_CURSOR_STRIP;
        scope_screen_measurement_bounds(&demo.screen,&mx,&my,&mw,&mh);
        if(x>=mx&&x<mx+mw&&y>=my&&y<my+mh)
            return y<my+30&&x>=mx+mw-(!demo.screen.measurement_hidden&&demo.screen.measurement_count>0&&
                   demo.screen.measurement_horizontal?95:69)?TOUCH_MEASURE_LAYOUT:TOUCH_MEASURE_DRAG;
        if((demo.screen.zoom_enabled||demo.screen.fft_enabled)&&
           abs(y-(SCOPE_PLOT_Y+demo.screen.split_height))<=15)return TOUCH_SPLIT;
        if(demo.screen.zoom_enabled&&y<SCOPE_PLOT_Y+demo.screen.split_height-26)return TOUCH_ZOOM_OVERVIEW;
        if(demo.screen.fft_enabled && y>=scope_screen_spectrum_top(&demo.screen))
            return demo.screen.fft_cursor_visible?TOUCH_FFT_CURSOR:TOUCH_NONE;
        if(demo.screen.zoom_enabled && y<SCOPE_PLOT_Y+demo.screen.split_height)return TOUCH_NONE;
        if(x>=SCOPE_WIDTH-36&&(abs(y-touch_screen_y(demo.screen.trigger_y))<=23||
           (demo.screen.trigger_preview&&abs(y-touch_screen_y(demo.screen.trigger_preview_y))<=23)))return TOUCH_PLOT_TRIGGER;
        if(x<40&&((demo.screen.ch1_enabled&&abs(y-touch_screen_y(demo.screen.channel_zero_y[0]))<44)||
           (demo.screen.ch2_enabled&&abs(y-touch_screen_y(demo.screen.channel_zero_y[1]))<44)))return TOUCH_TIME_IN;
        if(demo.screen.cursor_mode!=SCOPE_CURSOR_OFF) {
            int c=demo.screen.cursor_mode==SCOPE_CURSOR_TIME?x-SCOPE_PLOT_X:touch_plot_y(y);
            if(abs(c-demo.screen.cursor_a)<=64||abs(c-demo.screen.cursor_b)<=64)return TOUCH_PLOT_CURSOR;
        }
        if(x<190)return TOUCH_TIME_IN;
        if(x>=810)return TOUCH_TIME_OUT;
        { int channel=touch_channel_at(x,y,-1); if(channel>=0)return channel?TOUCH_PLOT_CH2:TOUCH_PLOT_CH1; }
    }
    return TOUCH_NONE;
}

static void touch_toggle_menu(DemoMenu kind)
{
    if(demo.screen.menu_open&&demo.menu_kind==kind)demo_signal_ui_dismiss_menu(&demo);
    else demo_signal_ui_open_menu(&demo,kind);
}

static void touch_short(void)
{
    DemoAction action=DEMO_ACTION_NONE;
    switch(touch.zone) {
        case TOUCH_TOP_MENU: demo_signal_ui_open_menu(&demo,demo.screen.response_visible?DEMO_MENU_RESPONSE:DEMO_MENU_MAIN); break;
        case TOUCH_BROWSER_BACK: demo_signal_ui_close_capture(&demo); break;
        case TOUCH_BROWSER_PREV: action=DEMO_ACTION_BROWSE_PREV; break;
        case TOUCH_BROWSER_NEXT: action=DEMO_ACTION_BROWSE_NEXT; break;
        case TOUCH_MENU_BACK: demo_signal_ui_menu_back(&demo); break;
        case TOUCH_MEASURE_CLEAR: demo_signal_ui_menu_select(&demo,SCOPE_MEASURE_CATALOG_ITEMS); demo_signal_ui_menu_activate(&demo); break;
        case TOUCH_MEASURE_HIDE: demo_signal_ui_menu_select(&demo,SCOPE_MEASURE_HIDE_INDEX);demo_signal_ui_menu_activate(&demo);break;
        case TOUCH_MEASURE_CANCEL: case TOUCH_MEASURE_CONFIRM:
            demo.screen.measurement_clear_choice=touch.zone==TOUCH_MEASURE_CONFIRM;
            demo_signal_ui_menu_activate(&demo); break;
        case TOUCH_BROWSE_DELETE_CANCEL: case TOUCH_BROWSE_DELETE_CONFIRM:
            demo.screen.browser_delete_choice=touch.zone==TOUCH_BROWSE_DELETE_CONFIRM;
            action=demo_signal_ui_menu_activate(&demo);break;
        case TOUCH_MENU_OUTSIDE: demo_signal_ui_dismiss_menu(&demo); break;
        case TOUCH_TOP_SCREENSHOT: action=DEMO_ACTION_SCREENSHOT; break;
        case TOUCH_TOP_WAVE: action=DEMO_ACTION_WAVEFORM; break;
        case TOUCH_TOP_FINE: demo_signal_ui_toggle_fine(&demo); break;
        case TOUCH_TOP_PROCESSING: touch_toggle_menu(DEMO_MENU_PROCESSING); break;
        case TOUCH_BOTTOM_CURSOR: touch_toggle_menu(DEMO_MENU_CURSOR); break;
        case TOUCH_TRIGGER_SOURCE: touch_toggle_menu(DEMO_MENU_TRIGGER); break;
        case TOUCH_TOP_RUN: if(demo.waveform_loaded)action=DEMO_ACTION_BROWSE_CLOSE;else demo_signal_ui_toggle_run(&demo);break;
        case TOUCH_CH1: touch_toggle_menu(DEMO_MENU_CH1); break;
        case TOUCH_CH2: touch_toggle_menu(DEMO_MENU_CH2); break;
        case TOUCH_TIME: touch_toggle_menu(DEMO_MENU_TIME); break;
        case TOUCH_TIME_IN: demo_signal_zoom_time(&demo,-1); break;
        case TOUCH_TIME_OUT: demo_signal_zoom_time(&demo,1); break;
        case TOUCH_MEASURE_LAYOUT:
            if(demo.screen.measurement_hidden)demo_signal_ui_toggle_measurements_visible(&demo);
            else if(demo.screen.measurement_count)demo_signal_ui_toggle_measurement_layout(&demo);
            else demo_signal_ui_open_menu(&demo,DEMO_MENU_MEASURE);
            break;
        case TOUCH_MEASURE_DRAG:
            if(demo.screen.measurement_hidden)demo_signal_ui_toggle_measurements_visible(&demo);
            else if(!demo.screen.measurement_count)demo_signal_ui_open_menu(&demo,DEMO_MENU_MEASURE);
            break;
        case TOUCH_CURSOR_STRIP: touch_toggle_menu(DEMO_MENU_CURSOR); break;
        case TOUCH_MENU_ROW:
            if(touch.x>=demo.screen.menu_x+(SCOPE_MENU_CONTROL_X-SCOPE_MENU_X)&&
               touch.x<demo.screen.menu_x+(SCOPE_MENU_CONTROL_X-SCOPE_MENU_X)+SCOPE_MENU_CONTROL_WIDTH&&
               demo.screen.menu_option_count[touch.row]) {
                int count=demo.screen.menu_option_count[touch.row];
                demo_signal_ui_menu_choose(&demo,touch.row,
                    (touch.x-demo.screen.menu_x-(SCOPE_MENU_CONTROL_X-SCOPE_MENU_X))*count/SCOPE_MENU_CONTROL_WIDTH);
            } else if(demo.screen.menu_stepper[touch.row]) {
                int control_x=demo.screen.menu_x+(SCOPE_MENU_CONTROL_X-SCOPE_MENU_X);
                if(touch.x>=control_x&&touch.x<control_x+56)
                    action=demo_signal_ui_menu_tap(&demo,touch.row,-1);
                else if(touch.x>=control_x+SCOPE_MENU_CONTROL_WIDTH-56&&
                        touch.x<control_x+SCOPE_MENU_CONTROL_WIDTH)
                    action=demo_signal_ui_menu_tap(&demo,touch.row,1);
                else { demo_signal_ui_menu_select(&demo,touch.row); action=demo_signal_ui_menu_activate(&demo); }
            } else action=demo_signal_ui_menu_tap(&demo,touch.row,1); break;
        case TOUCH_PLOT_CURSOR:
            demo_signal_ui_move_cursor(&demo,demo.screen.cursor_mode==SCOPE_CURSOR_TIME?
                                       touch.x-SCOPE_PLOT_X:touch_plot_y(touch.y));break;
        case TOUCH_ZOOM_OVERVIEW: demo_signal_set_zoom_center(&demo,touch.x-SCOPE_PLOT_X);break;
        default: break;
    }
    act(action);
}

static int fine_touch_delta(int delta, int *remainder)
{
    int total,scaled;
    if(!demo.screen.fine_mode){*remainder=0;return delta;}
    total=*remainder+delta;scaled=total/4;*remainder=total%4;return scaled;
}

static void pan_time_by_touch(int delta)
{
    delta=fine_touch_delta(delta,&touch.fine_x_remainder);
    if(delta)demo_signal_ui_pan_time(&demo,delta);
}

static void touch_move(int x, int y)
{
    int movement,threshold=30,steps;
    int motion_threshold=touch.zone==TOUCH_PLOT_TRIGGER||touch.zone==TOUCH_SPLIT||
        touch.zone==TOUCH_ZOOM_OVERVIEW||touch.zone==TOUCH_PLOT_CH1||touch.zone==TOUCH_PLOT_CH2||
        touch.zone==TOUCH_PLOT_CURSOR||touch.zone==TOUCH_MEASURE_DRAG||touch.zone==TOUCH_MEASURE_LAYOUT?3:12;
    if(touch.zone==TOUCH_MENU_DRAG)motion_threshold=3;
    if(!touch.active)return;
    if(touch.zone==TOUCH_FFT_CURSOR){
        if(x!=touch.last_x||y!=touch.last_y){
            int vertical=demo.screen.cursor_mode==SCOPE_CURSOR_VOLTAGE;
            int coordinate=vertical?scope_screen_fft_level_at(&demo.screen,y):x-SCOPE_PLOT_X;
            int previous=vertical?scope_screen_fft_level_at(&demo.screen,touch.last_y):touch.last_x-SCOPE_PLOT_X;
            demo_signal_ui_drag_fft_cursor(&demo,coordinate,previous,
                vertical?&touch.fine_y_remainder:&touch.fine_x_remainder);
            touch.last_x=x;touch.last_y=y;touch.moved=1;invalidate();}
        return;
    }
    if(abs(x-touch.x)>motion_threshold||abs(y-touch.y)>motion_threshold)touch.moved=1;
    if(!touch.moved)return;
    movement=x-touch.last_x;
    switch(touch.zone) {
        case TOUCH_MENU_DRAG:
            demo_signal_ui_move_menu(&demo,x-touch.last_x,y-touch.last_y);
            touch.last_x=x;touch.last_y=y;invalidate();return;
        case TOUCH_CH1: case TOUCH_CH2: case TOUCH_TIME:
            if(abs(x-touch.x)<=abs(y-touch.y)+4)return;
            touch.remainder+=movement;steps=touch.remainder/36;touch.remainder%=36;touch.last_x=x;touch.last_y=y;
            if(steps){
                if(touch.zone==TOUCH_TIME)demo_signal_zoom_time(&demo,steps);
                else demo_signal_zoom_channel(&demo,touch.zone==TOUCH_CH2,-steps);
                invalidate();
            }
            return;
        case TOUCH_MENU_ROW: {
            int row;
            if(demo.screen.measurement_menu) {
                int local_row=(y-SCOPE_MEASURE_MENU_ROW_Y)/SCOPE_MEASURE_MENU_ROW_HEIGHT;
                if(local_row<0)local_row=0;
                if(local_row>7)local_row=7;
                row=(x>=512?8:0)+local_row;
                if(row>=demo.screen.menu_count)row=demo.screen.menu_count-1;
            } else {
                int row_y=demo.screen.menu_y+(SCOPE_MENU_ROW_Y-SCOPE_MENU_Y);
                row=(y-row_y)/SCOPE_MENU_ROW_HEIGHT;
                if(y<row_y)row=0;
                if(row>=demo.screen.menu_count)row=demo.screen.menu_count-1;
            }
            demo_signal_ui_menu_select(&demo,row);
            touch.last_x=x;touch.last_y=y;invalidate();return;
        }
        case TOUCH_PLOT_TRIGGER:
            if(demo.screen.fine_mode){int d=fine_touch_delta(touch_plot_y(y)-touch_plot_y(touch.last_y),&touch.fine_y_remainder);
                if(d)demo_signal_set_trigger_preview_y(&demo,(demo.screen.trigger_preview?demo.screen.trigger_preview_y:demo.screen.trigger_y)+d);}
            else demo_signal_set_trigger_preview_y(&demo,touch_plot_y(y));
            touch.last_y=y;invalidate();return;
        case TOUCH_SPLIT:
            if(demo.screen.fine_mode){int d=fine_touch_delta(y-touch.last_y,&touch.fine_y_remainder);
                if(d)demo_signal_ui_set_split_height(&demo,demo.screen.split_height+d);}
            else demo_signal_ui_set_split_height(&demo,y-SCOPE_PLOT_Y);
            touch.last_y=y;invalidate();return;
        case TOUCH_PLOT_CH1: case TOUCH_PLOT_CH2:
            if(!touch.axis){int dx=abs(x-touch.x),dy=abs(y-touch.y);if(dx+dy<7)return;touch.axis=dy>=dx?1:2;}
            if(touch.axis==1){int d=fine_touch_delta(touch_plot_y(y)-touch_plot_y(touch.last_y),&touch.fine_y_remainder);
                if(d)demo_signal_move_channel(&demo,touch.zone==TOUCH_PLOT_CH2,d);}else pan_time_by_touch(x-touch.last_x);
            touch.last_x=x;touch.last_y=y;invalidate();return;
        case TOUCH_ZOOM_OVERVIEW:
            if(demo.screen.fine_mode){int d=fine_touch_delta(x-touch.last_x,&touch.fine_x_remainder);
                if(d)demo_signal_set_zoom_center(&demo,(demo.screen.zoom_window_start+demo.screen.zoom_window_end)/2+d);}
            else demo_signal_set_zoom_center(&demo,x-SCOPE_PLOT_X);
            touch.last_x=x;invalidate();return;
        case TOUCH_PLOT_CURSOR:
            if(demo.screen.fine_mode){int d=demo.screen.cursor_mode==SCOPE_CURSOR_TIME?
                fine_touch_delta(x-touch.last_x,&touch.fine_x_remainder):
                fine_touch_delta(touch_plot_y(y)-touch_plot_y(touch.last_y),&touch.fine_y_remainder);
                if(d)demo_signal_ui_move_cursor(&demo,(demo.screen.cursor_selected?demo.screen.cursor_b:demo.screen.cursor_a)+d);}
            else demo_signal_ui_move_cursor(&demo,demo.screen.cursor_mode==SCOPE_CURSOR_TIME?x-SCOPE_PLOT_X:touch_plot_y(y));
            touch.last_x=x;touch.last_y=y;invalidate();return;
        case TOUCH_TIME_IN: case TOUCH_TIME_OUT:
            if(abs(y-touch.y)>abs(x-touch.x)+4){int channel=touch_channel_at(touch.x,touch.y,-1);if(channel>=0){
                int d;touch.zone=channel?TOUCH_PLOT_CH2:TOUCH_PLOT_CH1;touch.axis=1;
                d=fine_touch_delta(touch_plot_y(y)-touch_plot_y(touch.last_y),&touch.fine_y_remainder);
                if(d)demo_signal_move_channel(&demo,channel,d);
                touch.last_x=x;touch.last_y=y;invalidate();
            }}
            return;
        case TOUCH_MEASURE_DRAG: case TOUCH_MEASURE_LAYOUT:
            demo_signal_ui_move_measurements(&demo,fine_touch_delta(x-touch.last_x,&touch.fine_x_remainder),
                fine_touch_delta(y-touch.last_y,&touch.fine_y_remainder));touch.last_x=x;touch.last_y=y;invalidate();return;
        default:return;
    }
    touch.remainder+=movement;steps=touch.remainder/threshold;touch.remainder%=threshold;touch.last_x=x;touch.last_y=y;
    if(steps){demo_signal_ui_menu_adjust(&demo,steps);invalidate();}
}

static void fire_lcd_touch_hold(uint64_t now)
{
    if(!touch.active||touch.moved||touch.long_done||now-touch.since<(uint64_t)demo.hold_time_ms)return;
    switch(touch.zone) {
        case TOUCH_TOP_MENU:demo_signal_ui_toggle_zoom(&demo);break;
        case TOUCH_CH1:demo_signal_ui_reset_channel_position(&demo,0);break;
        case TOUCH_CH2:demo_signal_ui_reset_channel_position(&demo,1);break;
        case TOUCH_TIME:demo_signal_ui_reset_time_position(&demo);break;
        case TOUCH_TRIGGER_SOURCE:demo_signal_ui_cycle_trigger_mode(&demo);break;
        case TOUCH_BOTTOM_CURSOR:demo_signal_ui_cycle_cursor_mode(&demo);break;
        case TOUCH_MEASURE_DRAG:case TOUCH_MEASURE_LAYOUT:demo_signal_ui_open_menu(&demo,DEMO_MENU_MEASURE);touch.zone=TOUCH_NONE;break;
        case TOUCH_MENU_ROW:
            if(demo.menu_kind!=DEMO_MENU_BROWSE||!demo.browse_count)return;
            demo_signal_ui_request_browse_delete(&demo,touch.row);touch.zone=TOUCH_NONE;break;
        default:return;
    }
    touch.long_done=1;invalidate();
}

static void lcd_touch_begin(int x, int y)
{
    if(touch.active)return;
    touch.zone=touch_zone_at(x,y,&touch.row);if(touch.zone==TOUCH_NONE)return;
    touch.x=touch.last_x=x;touch.y=touch.last_y=y;touch.remainder=0;
    touch.fine_x_remainder=touch.fine_y_remainder=0;
    touch.axis=touch.moved=touch.long_done=0;touch.since=now_ms();touch.active=1;
    if(touch.zone==TOUCH_FFT_CURSOR){
        if(demo.screen.fine_mode)demo_signal_ui_select_cursor(&demo,SCOPE_CURSOR_SELECT_FFT);
        else demo_signal_ui_move_fft_cursor(&demo,demo.screen.cursor_mode==SCOPE_CURSOR_VOLTAGE?
            scope_screen_fft_level_at(&demo.screen,y):x-SCOPE_PLOT_X);
        invalidate();}
    if(touch.zone==TOUCH_PLOT_CURSOR){int c=demo.screen.cursor_mode==SCOPE_CURSOR_TIME?x-SCOPE_PLOT_X:touch_plot_y(y);
        demo_signal_ui_select_cursor(&demo,abs(c-demo.screen.cursor_b)<abs(c-demo.screen.cursor_a));}
}

static void lcd_touch_end(int y)
{
    if(!touch.active)return;
    fire_lcd_touch_hold(now_ms());
    if(touch.zone==TOUCH_PLOT_TRIGGER){if(!demo.screen.fine_mode)demo_signal_set_trigger_preview_y(&demo,touch_plot_y(y));
        demo_signal_ui_apply_trigger(&demo);}else if(!touch.moved&&!touch.long_done)touch_short();
    touch.active=0;invalidate();
}

static int contact_count(void){return contacts[0].active+contacts[1].active;}
static uint64_t contact_distance_sq(void){int64_t dx=contacts[0].x-contacts[1].x,dy=contacts[0].y-contacts[1].y;return(uint64_t)(dx*dx+dy*dy);}

static void handle_finger(const SDL_TouchFingerEvent *event, int kind)
{
    int x=(int)(event->x*SCOPE_WIDTH),y=(int)(event->y*SCOPE_HEIGHT),i,slot=-1;
    for(i=0;i<2;++i)if(contacts[i].active&&contacts[i].id==event->fingerId)slot=i;
    if(kind==SDL_FINGERDOWN){if(slot<0)for(i=0;i<2;++i)if(!contacts[i].active){slot=i;break;}if(slot<0)return;
        contacts[slot].id=event->fingerId;contacts[slot].x=x;contacts[slot].y=y;contacts[slot].active=1;
        if(contact_count()==1&&!pinch_block)lcd_touch_begin(x,y);else if(contact_count()==2){pinch_block=1;pinch_consumed=0;pinch_start_sq=contact_distance_sq();touch.active=0;}
    } else if(slot>=0&&kind==SDL_FINGERMOTION){contacts[slot].x=x;contacts[slot].y=y;
        if(contact_count()==2&&!pinch_consumed&&pinch_start_sq>100){uint64_t distance=contact_distance_sq();
            if(!demo.screen.zoom_enabled&&distance*100>pinch_start_sq*125){demo_signal_ui_toggle_zoom(&demo);pinch_consumed=1;invalidate();}
            else if(demo.screen.zoom_enabled&&distance*100<pinch_start_sq*80){demo_signal_ui_toggle_zoom(&demo);pinch_consumed=1;invalidate();}}
        else if(!pinch_block&&contact_count()==1)touch_move(x,y);
    } else if(slot>=0&&kind==SDL_FINGERUP){if(!pinch_block&&contact_count()==1)lcd_touch_end(y);contacts[slot].active=0;
        if(!contact_count()){pinch_block=0;pinch_start_sq=0;}}
}

static DemoControl key_control(SDL_Keycode key)
{
    switch(key){case SDLK_1:return DEMO_BTN_CH1;case SDLK_2:return DEMO_BTN_CH2;
        case SDLK_SPACE:return DEMO_BTN_RUN;case SDLK_RETURN:case SDLK_KP_ENTER:return DEMO_ENC_TRIGGER;
        case SDLK_c:return DEMO_BTN_CURSOR;case SDLK_t:return DEMO_BTN_MODE;case SDLK_m:return DEMO_BTN_MENU;
        default:return DEMO_CONTROL_COUNT;}
}

static void key_down(const SDL_KeyboardEvent *event)
{
    DemoControl control;uint64_t now=now_ms();
    if(event->repeat&&event->keysym.sym!=SDLK_UP&&event->keysym.sym!=SDLK_DOWN)return;
    if(event->keysym.sym==SDLK_UP||event->keysym.sym==SDLK_DOWN){keyboard_control_send(&keyboard_controls,CONTROL_ROTATE,DEMO_ENC_TRIGGER,event->keysym.sym==SDLK_UP?1:-1,(uint32_t)now);return;}
    control=key_control(event->keysym.sym);if(control==DEMO_CONTROL_COUNT)return;
    keyboard_control_send(&keyboard_controls,CONTROL_DOWN,control,0,(uint32_t)now);
    invalidate();
}

static void key_up(const SDL_KeyboardEvent *event)
{
    DemoControl control;if(event->keysym.sym==SDLK_F2||event->keysym.sym==SDLK_F3)
        act(event->keysym.sym==SDLK_F2?DEMO_ACTION_SCREENSHOT:DEMO_ACTION_SCREEN_AND_WAVE);
    control=key_control(event->keysym.sym);
    if(control!=DEMO_CONTROL_COUNT)keyboard_control_send(&keyboard_controls,CONTROL_UP,control,0,(uint32_t)now_ms());
    invalidate();
}

static int update_panel_battery(const SerialControlMetrics *panel,uint64_t now)
{
    return demo_signal_set_battery(&demo,(unsigned)(panel->power_mv>0?panel->power_mv:0),
        panel->connected&&panel->power_received_ms&&now>=panel->power_received_ms&&
        now-panel->power_received_ms<=3000);
}

/* Match channel ON/OFF to physical relays without taking over the panel's
   ordinary range encoder. Reapply after a panel reset or a USB reconnect. */
static void sync_panel_ranges(const SerialControlMetrics *panel,uint64_t now)
{
    const DemoSignal *live=live_settings();
    unsigned ch;
    for(ch=0;ch<2;++ch){
        int target=demo_signal_panel_scale(live,ch),enabled=target!=CONTROL_SCALE_OFF;
        if(!panel->connected){panel_channel_enabled[ch]=-1;continue;}
        if(panel_channel_enabled[ch]!=enabled || ((panel->scale_valid&(1U<<ch)) &&
            (panel->scale_mv[ch]!=CONTROL_SCALE_OFF)!=enabled)){
            restored_scale[ch]=target;panel_channel_enabled[ch]=enabled;
        }
    }
    if(now>=restore_retry_ms){
        for(ch=0;ch<2;++ch)if(restored_scale[ch]>=0)
            serial_control_set_scale(&serial_controls,ch,restored_scale[ch]);
        restore_retry_ms=now+100;
    }
}

static void drain_controls(uint64_t now)
{
    static unsigned generation;static uint32_t down_ms[CONTROL_ID_COUNT];
    ControlEvent event;unsigned next=control_queue_generation(&controls);
    if(next!=generation){memset(key_holds,0,sizeof(key_holds));pending_chord=DEMO_CONTROL_COUNT;generation=next;}
    while(control_queue_pop(&controls,&event)){
        if(event.type==CONTROL_SCALE){
            if(event.control<2 && panel_channel_enabled[event.control]>=0 &&
               (demo_signal_panel_scale(live_settings(),event.control)!=CONTROL_SCALE_OFF)!=
                panel_channel_enabled[event.control])continue;
            if(event.control<2 && restored_scale[event.control]>=0){
                if(event.value!=restored_scale[event.control])continue;
                restored_scale[event.control]=-1;
            }
            demo_signal_accept_scale(&demo,event.control,event.value);invalidate();continue;
        }
        if(event.control>=CONTROL_ID_COUNT)continue;
        if(event.type==CONTROL_ROTATE){pending_chord=DEMO_CONTROL_COUNT;demo_signal_rotate(&demo,(DemoControl)event.control,event.value);}
        else if(event.type==CONTROL_DOWN&&!key_holds[event.control].active){down_ms[event.control]=event.timestamp_ms;
            begin_press(&key_holds[event.control],(DemoControl)event.control,0,now);}
        else if(event.type==CONTROL_UP&&key_holds[event.control].active){
            /* Source duration survives UART burst delivery and uint32 timestamp wrap. */
            uint32_t duration=event.timestamp_ms-down_ms[event.control];
            HeldControl *held=&key_holds[event.control];
            if(!held->long_done&&!held->suppressed)
                press_control_at(held->control,has_hold_action(held->control)&&
                    duration>=(uint32_t)demo.hold_time_ms,now);
            held->active=0;
        }
        invalidate();
    }
}

static void logical_point(SDL_Window *window, int logical_w, int logical_h, int *x, int *y)
{
    int w,h;SDL_GetWindowSize(window,&w,&h);if(w>0)*x=*x*logical_w/w;if(h>0)*y=*y*logical_h/h;
}

static SDL_Renderer *make_renderer(SDL_Window *window)
{
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    return renderer;
}

static SDL_Window *create_kiosk_window(void)
{
    SDL_Window *window;
    window = SDL_CreateWindow("Oscill LCD", SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
                              SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
                              SCOPE_WIDTH, SCOPE_HEIGHT,
                              SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    if (!window) return NULL;
    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SDL_DestroyWindow(window);
        return NULL;
    }
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_ShowCursor(SDL_DISABLE);
    return window;
}

static void determine_application_dir(const char *argv0)
{
    char path[PATH_MAX],*slash;ssize_t length=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(length<0){if(!realpath(argv0,path))snprintf(path,sizeof(path),"%s",argv0);length=(ssize_t)strlen(path);}
    path[length]='\0';slash=strrchr(path,'/');if(slash)*slash='\0';else snprintf(path,sizeof(path),".");
    snprintf(application_dir,sizeof(application_dir),"%s",path);
}

static int unsigned_option(const char *text, unsigned *value)
{
    char *end;unsigned long number;
    if(!text[0]||text[0]=='-')return 0;
    errno=0;number=strtoul(text,&end,10);
    if(errno||*end||number>UINT_MAX)return 0;
    *value=(unsigned)number;return 1;
}

static void remote_state(FILE *response)
{
    int channel,i;
    uint64_t frames=0,render_ns=0,output_ns=0;CaptureMetrics capture={0};
    SerialControlMetrics panel={0};int mx,my,mw,mh;
    serial_control_metrics(&serial_controls,&panel);
    scope_screen_measurement_bounds(&demo.screen,&mx,&my,&mw,&mh);
    if(renderer)scope_renderer_metrics(renderer,&frames,&render_ns,&output_ns);
    if(capture_pipeline)capture_pipeline_metrics(capture_pipeline,&capture);
    fprintf(response,"{\"version\":1,\"source\":\"%s\",\"running\":%d,\"waiting\":%d,\"plot_height\":%d,\"pixels_per_div\":%.9g,\"time_divisions\":10,",
        capture_pipeline?"raw":"demo",demo.screen.running,demo.screen.waiting_for_trigger,SCOPE_PLOT_HEIGHT,SCOPE_PLOT_HEIGHT/8.0);
    for(channel=0;channel<2;++channel){
        fprintf(response,"\"ch%d_enabled\":%d,\"ch%d_coupling\":%d,\"ch%d_probe_ten\":%d,\"ch%d_scale_index\":%d,\"ch%d_position\":%d,\"ch%d_filter\":%d,",
            channel+1,channel?demo.screen.ch2_enabled:demo.screen.ch1_enabled,channel+1,demo.coupling[channel],
            channel+1,demo.probe_ten[channel],channel+1,demo.scale_index[channel],channel+1,demo.channel_position[channel],channel+1,demo.filter_level[channel]);
    }
    if(capture_frame_valid){
        const DisplayFrame *f=leased_frame?leased_frame:&capture_frame;
        for(unsigned ch=0;ch<2;++ch){
            fprintf(response,"\"ch%u_frequency_hz\":",ch+1);
            if(f->frequency_hz[ch]>0)fprintf(response,"%.9g,",f->frequency_hz[ch]);else fputs("null,",response);
            fprintf(response,"\"ch%u_duty_percent\":",ch+1);
            if(isfinite(f->duty[ch]))fprintf(response,"%.9g,",f->duty[ch]);else fputs("null,",response);
            const char *names[]={"vpp","minimum","maximum","mean","rms","raw_mean","raw_rms"};
            const double values[]={f->maximum[ch]-f->minimum[ch],f->minimum[ch],f->maximum[ch],
                f->mean[ch],f->rms[ch],f->raw_mean[ch],f->raw_rms[ch]};
            int enabled=ch?demo.screen.ch2_enabled:demo.screen.ch1_enabled;
            for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i){
                fprintf(response,"\"ch%u_%s_volts\":",ch+1,names[i]);
                if(enabled && isfinite(values[i]))fprintf(response,"%.9g,",values[i]);else fputs("null,",response);
            }
        }
    }
    uint64_t presented_count=0,presented_at=0;
    if(renderer)scope_renderer_presentation(renderer,&presented_count,&presented_at);
    fprintf(response,"\"presented_at_ns\":%llu,\"presented_plots\":%llu,",
        (unsigned long long)presented_at,(unsigned long long)presented_count);
    fprintf(response,"\"capture_blocks\":%llu,\"capture_dropped\":%llu,\"capture_overwritten\":%llu,\"source_error\":%d,",
        (unsigned long long)capture.blocks,(unsigned long long)capture.dropped,
        (unsigned long long)capture.overwritten,capture.source_error);
    fprintf(response,"\"capture_acquisition_ns\":%llu,\"capture_search_ns\":%llu,\"capture_search_max_ns\":%llu,",
        (unsigned long long)capture.acquisition_ns,(unsigned long long)capture.search_ns,
        (unsigned long long)capture.search_max_ns);
    fprintf(response,"\"capture_rejected\":%llu,",(unsigned long long)capture.rejected_blocks);
    fprintf(response,"\"capture_deadtime\":%llu,",(unsigned long long)capture.deadtime_blocks);
    fprintf(response,"\"capture_probe_blocks\":%llu,",(unsigned long long)capture.probe_blocks);
    fprintf(response,"\"capture_dsp_blocks\":%llu,\"capture_dsp_missed\":%llu,",
        (unsigned long long)capture.dsp_blocks,(unsigned long long)capture.dsp_missed);
    fprintf(response,"\"record_depth_index\":%d,\"sample_rate_hz\":%.9g,",demo.record_depth_index,demo.acquisition_rate_hz);
    fprintf(response,"\"measurement_hidden\":%d,\"measurement_x\":%d,\"measurement_y\":%d,\"measurement_width\":%d,\"measurement_height\":%d,",
        demo.screen.measurement_hidden,mx,my,mw,mh);
    fprintf(response,"\"panel_scale_valid\":%u,\"ch1_panel_scale_mv\":%d,\"ch2_panel_scale_mv\":%d,",
        panel.scale_valid,panel.scale_mv[0],panel.scale_mv[1]);
    fprintf(response,"\"zero_measuring\":%d,\"noise_measuring\":%d,\"zero_frames\":%d,\"calibration_range\":%d,\"calibration_returning\":%d,",demo.baseline_active==CHANNEL_ZERO_VALID,demo.baseline_active==CHANNEL_NOISE_VALID,demo.baseline_frames,demo.baseline_stage,demo.calibration_scale_target>=0);
    for(channel=0;channel<2;++channel){ChannelBaseline *b=demo_signal_baseline(&demo,(unsigned)channel);
        unsigned zero_ranges=0,noise_ranges=0,bank=(unsigned)channel+(demo.adc_simulation?2:0);
        for(unsigned stage=0;stage<INPUT_RANGE_STAGE_COUNT;++stage){
            if(demo.baseline[bank][stage].valid&CHANNEL_ZERO_VALID)zero_ranges|=1U<<stage;
            if(demo.baseline[bank][stage].valid&CHANNEL_NOISE_VALID)noise_ranges|=1U<<stage;
        }
        fprintf(response,"\"ch%d_zero_ranges\":%u,\"ch%d_noise_ranges\":%u,",channel+1,zero_ranges,channel+1,noise_ranges);
        fprintf(response,"\"ch%d_zero_known\":%d,\"ch%d_noise_known\":%d,\"ch%d_zero\":%.9g,\"ch%d_noise_rms\":%.9g,",
            channel+1,!!(b->valid&CHANNEL_ZERO_VALID),channel+1,!!(b->valid&CHANNEL_NOISE_VALID),channel+1,b->zero,channel+1,b->noise_rms);}
    fprintf(response,"\"startup_preset\":%d,\"preset_valid\":%u,",demo.startup_preset,demo.preset_valid);
    fprintf(response,"\"calibration_active\":%d,\"calibration_channel\":%d,\"ch1_response_points\":%u,\"ch2_response_points\":%u,\"ch1_response\":%d,\"ch2_response\":%d,",
        demo.response_active,demo.response_channel+1,demo_signal_response_curve(&demo,0)->count,
        demo_signal_response_curve(&demo,1)->count,demo.response_enabled[0],demo.response_enabled[1]);
    fprintf(response,"\"simulation\":%d,\"generator_frequency_index\":%d,\"ch1_wave\":%d,\"ch2_wave\":%d,\"fft\":%d,\"fft_size\":%u,\"peak_detect\":%d,\"intensity_coloring\":%d,\"battery_known\":%d,\"battery_percent\":%d,",
        demo.adc_simulation,demo.generator_frequency_index,demo.generator_wave[0],demo.generator_wave[1],
        demo.fft_enabled,demo.fft_size,demo.screen.peak_detect,demo.intensity_coloring,
        demo.screen.battery_known,demo.screen.battery_percent);
    fprintf(response,"\"time_index\":%d,\"time_position\":%d,\"zoom\":%d,\"trigger_mode\":%d,\"trigger_source\":%d,\"trigger_edge\":%d,\"trigger_holdoff\":%d,\"trigger_level\":%.9g,\"time_scales_us\":[",
        demo.screen.zoom_enabled?demo.zoom_time_index:demo.time_index,demo.time_position,demo.screen.zoom_enabled,
        demo.trigger_mode_index,demo.trigger_source_index,demo.trigger_edge_index,demo.trigger_holdoff_index,demo.trigger_voltage);
    for(i=0;demo_signal_time_scale_us(i)>0;++i)fprintf(response,"%s%.9g",i?",":"",demo_signal_time_scale_us(i));
    fputs("],\"channel_scales_mv\":[",response);
    for(i=0;demo_signal_scale_mv(i)>0;++i)fprintf(response,"%s%d",i?",":"",demo_signal_scale_mv(i));
    fprintf(response,"],\"uart_baud\":%u,\"pc_active\":%d,\"render_frames\":%llu,\"render_ns\":%llu,\"output_ns\":%llu,\"processing_ns\":%llu,\"waveforms\":%llu,\"triggered_ns\":%llu,\"presented_ns\":%llu,\"menu_open\":%d,\"menu_kind\":%d,\"menu_selected\":%d}\n",
        console_baud,demo.pc_active,(unsigned long long)frames,(unsigned long long)render_ns,
        (unsigned long long)output_ns,(unsigned long long)capture.processing_ns,(unsigned long long)capture.waveforms,(unsigned long long)capture.triggered_ns,
        (unsigned long long)(renderer?scope_renderer_presented_plot(renderer):0),
        demo.screen.menu_open,demo.menu_kind,demo.screen.menu_selected);
}

static int remote_set(const char *key,double number)
{
    static const struct {const char *key;DemoMenu kind;int item;} options[]={
        {"ch1_enabled",DEMO_MENU_CH1,0},{"ch2_enabled",DEMO_MENU_CH2,0},
        {"ch1_coupling",DEMO_MENU_CH1,1},{"ch2_coupling",DEMO_MENU_CH2,1},
        {"ch1_probe_ten",DEMO_MENU_CH1,2},{"ch2_probe_ten",DEMO_MENU_CH2,2},
        {"ch1_scale_index",DEMO_MENU_CH1,3},{"ch2_scale_index",DEMO_MENU_CH2,3},
        {"ch1_filter",DEMO_MENU_PROCESSING,0},{"ch2_filter",DEMO_MENU_PROCESSING,1},
        {"record_depth_index",DEMO_MENU_PROCESSING,5},{"time_index",DEMO_MENU_TIME,0},{"trigger_mode",DEMO_MENU_TRIGGER,0},
        {"startup_preset",DEMO_MENU_SETUP,0},
        {"generator_frequency_index",DEMO_MENU_GENERATOR,2},{"ch1_wave",DEMO_MENU_GENERATOR,0},
        {"ch2_wave",DEMO_MENU_GENERATOR,1},{"simulation",DEMO_MENU_DEBUG,3},
        {"fft",DEMO_MENU_PROCESSING,4},{"persistence",DEMO_MENU_PROCESSING,3},
        {"intensity_coloring",DEMO_MENU_PROCESSING,2},
        {"trigger_source",DEMO_MENU_TRIGGER,1},{"trigger_edge",DEMO_MENU_TRIGGER,2},
        {"trigger_holdoff",DEMO_MENU_TRIGGER,3}
    };
    size_t i;int value;
    if(!isfinite(number)||number < -1000000||number>1000000)return 0;
    if(!strcmp(key,"trigger_level")){demo_signal_set_trigger_voltage(&demo,number);return 1;}
    if(floor(number)!=number)return 0;value=(int)number;
    for(i=0;i<sizeof(options)/sizeof(options[0]);++i)if(!strcmp(key,options[i].key))
        return demo_signal_set_option(&demo,options[i].kind,options[i].item,value);
    if((!strcmp(key,"ch1_position")||!strcmp(key,"ch2_position"))&&value>=-180&&value<=180){
        int channel=key[2]=='2';demo_signal_move_channel(&demo,channel,value-demo.channel_position[channel]);return 1;}
    if(!strcmp(key,"time_position")&&value>=-420&&value<=420&&!demo.screen.zoom_enabled){
        demo_signal_ui_pan_time(&demo,demo.time_position-value);return 1;}
    if((!strcmp(key,"ch1_response")||!strcmp(key,"ch2_response")) && value>=0 && value<=1){
        int ok=demo_signal_response_enable(&demo,(unsigned)(key[2]=='2'),value);
        if(ok)demo_signal_refresh(&demo);return ok;
    }
    return 0;
}

static int remote_command(const char *command, FILE *response, void *context)
{
    char key[64],operation[32];double value;int n=0;unsigned channel;
    (void)context;
    pc_activity_until = now_ms() + 10000;
    int pc_was_active=demo.pc_active;
    demo_signal_pc_status(&demo, demo.pc_available, console_baud, 1);
    if(demo.pc_active!=pc_was_active)dirty=overlay_dirty=1;
    if(!strcmp(command,"ping")){fputs("OSCILL REMOTE 1\n",response);return 1;}
    if(!strcmp(command,"state")){remote_state(response);return !ferror(response);}
    if(!strcmp(command,"csv")||!strcmp(command,"window")){
        DemoWaveCapture wave;
        if(!strcmp(command,"window"))remote_state(response);
        demo_signal_export_wave(&demo,&wave);return wave_file_write(response,&wave);
    }
    if(sscanf(command,"response %u%n",&channel,&n)==1 && command[n]=='\0' && channel>=1 && channel<=2){
        const ChannelResponse *curve=demo_signal_response_curve(&demo,channel-1);
        fprintf(response,"{\"reference\":%.17g,\"points\":[",curve->reference);
        for(unsigned i=0;i<curve->count;++i)fprintf(response,"%s[%.17g,%.17g]",i?",":"",curve->frequency[i],curve->db[i]);
        fputs("]}\n",response);return 1;
    }
    if(sscanf(command,"calibrate %u %31s%n",&channel,operation,&n)==2 && command[n]=='\0' && channel>=1 && channel<=2){
        if(!strcmp(operation,"start") || !strcmp(operation,"show") || !strcmp(operation,"zero") || !strcmp(operation,"noise") || !strcmp(operation,"reset_zero") || !strcmp(operation,"reset_noise") || !strcmp(operation,"menu")){
            demo_signal_ui_open_menu(&demo,channel==1?DEMO_MENU_CH1:DEMO_MENU_CH2);
            demo_signal_ui_menu_select(&demo,4);act(demo_signal_ui_menu_activate(&demo));
            if(demo.menu_kind!=DEMO_MENU_CALIBRATION)return 0;
            if(strcmp(operation,"menu")){
                int zero=!strcmp(operation,"zero") || !strcmp(operation,"reset_zero");
                int noise=!strcmp(operation,"noise") || !strcmp(operation,"reset_noise");
                demo_signal_ui_menu_select(&demo,zero?0:noise?1:2);act(demo_signal_ui_menu_activate(&demo));
                if(zero || noise){demo_signal_ui_menu_select(&demo,!strncmp(operation,"reset_",6)?1:0);act(demo_signal_ui_menu_activate(&demo));}
                else if(!strcmp(operation,"start"))act(demo_signal_ui_menu_activate(&demo));
                else demo_signal_ui_dismiss_menu(&demo);
            }
        }else if(!strcmp(operation,"stop")){
            if(demo.response_active && demo.response_channel==(int)channel-1)demo_signal_ui_toggle_run(&demo);
        }else if(!strcmp(operation,"clear")){
            memset(demo_signal_response_curve(&demo,channel-1),0,sizeof(ChannelResponse));
            demo.response_enabled[channel-1]=0;demo_signal_refresh(&demo);
        }else if(!strcmp(operation,"exit")){
            demo_signal_baseline_cancel(&demo);demo.response_active=0;demo.screen.response_visible=0;demo_signal_ui_dismiss_menu(&demo);demo_signal_refresh(&demo);
        }else return 0;
    }
    else if(sscanf(command,"set %63s %lf%n",key,&value,&n)==2&&command[n]=='\0'&&remote_set(key,value)){}
    else if(sscanf(command,"preset_save %lf%n",&value,&n)==1 && command[n]=='\0' &&
            isfinite(value) && floor(value)==value && value>=1 && value<=5 && settings.store){
        demo.preset_selected=(int)value-1;act(DEMO_ACTION_PRESET_SAVE);
    }
    else if(sscanf(command,"preset_load %lf%n",&value,&n)==1 && command[n]=='\0' &&
            isfinite(value) && floor(value)==value && value>=1 && value<=5 &&
            (settings.document.valid&(1U<<((unsigned)value-1)))){
        demo.preset_selected=(int)value-1;act(DEMO_ACTION_PRESET_LOAD);
    }
    else if(!strcmp(command,"run")){if(!demo.screen.running)demo_signal_ui_toggle_run(&demo);}
    else if(!strcmp(command,"stop")){if(demo.screen.running)demo_signal_ui_toggle_run(&demo);}
    else if(!strcmp(command,"single")){demo_signal_set_option(&demo,DEMO_MENU_TRIGGER,0,2);if(!demo.screen.running)demo_signal_ui_toggle_run(&demo);}
    else if(!strcmp(command,"force"))demo_signal_force_trigger(&demo);
    else{fputs("Invalid remote command or setting\n",response);return 0;}
    invalidate();fputs("OK\n",response);return 1;
}

static void publish_service_status(int fd, const char *path, unsigned stage, unsigned source,
                                   int panel_enabled_status, int touch_status, uint64_t frames,
                                   uint64_t render_ns)
{
    StatusPacket packet; CaptureMetrics capture={0}; SerialControlMetrics panel={0};
    status_packet_init(&packet);
    if(capture_pipeline)capture_pipeline_metrics(capture_pipeline,&capture);
    serial_control_metrics(&serial_controls,&panel);
    packet.stage=stage;packet.pid=(uint32_t)getpid();packet.source=source;
    packet.running=demo.screen.running;packet.frames=frames;packet.render_ns=render_ns;
    packet.pipeline=capture_pipeline!=NULL && !demo.waveform_loaded;
    packet.blocks=capture.blocks;packet.bytes=capture.bytes;packet.processing_ns=capture.processing_ns;
    packet.dropped=capture.dropped;packet.overwritten=capture.overwritten;
    packet.occupancy=(uint32_t)capture.occupancy;packet.source_error=capture.source_error;
    packet.adc_valid=capture.adc_valid;packet.adc_age_ms=capture.adc_age_ms;
    memcpy(packet.adc_code,capture.adc_code,sizeof(packet.adc_code));
    packet.panel=panel_enabled_status?panel.connected:-1;packet.touch=touch_status;
    packet.uart_errors=panel.crc_errors+panel.format_errors+panel.read_errors+panel.write_errors;
    packet.uart_gaps=panel.sequence_gaps;
    /* UART timestamps use CLOCK_MONOTONIC, not SDL's time since initialization. */
    uint64_t panel_now=scope_clock_ns()/1000000;
    if(panel.power_received_ms && panel_now>=panel.power_received_ms) {
        packet.power_mv=panel.power_mv;packet.power_age_ms=panel_now-panel.power_received_ms;
    }
    if(panel.temperature_valid && panel.temperature_received_ms && panel_now>=panel.temperature_received_ms) {
        packet.stm_temperature_valid=1;packet.stm_temperature_mc=panel.temperature_mc;
        packet.stm_temperature_age_ms=panel_now-panel.temperature_received_ms;
    }
    if(stage==STATUS_FAILED)snprintf(packet.error,sizeof(packet.error),"%s",*startup_error?startup_error:"GUI START OR DISPLAY");
    else if(demo.screen.status_visible && demo.screen.status_warning && demo.screen.status_message)
        snprintf(packet.error,sizeof(packet.error),"%s",demo.screen.status_message);
    status_send(fd,path,&packet);
}

#ifdef SCOPE_ENABLE_ZYNQ
typedef struct {
    int status_fd;
    const char *status_path;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    LinuxDisplay *display;
#endif
} StartupView;

static void startup_show(StartupView *view,const char *message,unsigned percent,int failed)
{
    StatusPacket packet;
    scope_startup_render(screen_pixels,SCOPE_WIDTH,message,percent,failed);
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    if(view->display)linux_display_present(view->display,screen_pixels);
    else
#endif
    if(view->renderer && view->texture){
        SDL_UpdateTexture(view->texture,NULL,screen_pixels,SCOPE_WIDTH*4);
        SDL_RenderClear(view->renderer);SDL_RenderCopy(view->renderer,view->texture,NULL,NULL);
        SDL_RenderPresent(view->renderer);
    }
    status_packet_init(&packet);packet.stage=failed?STATUS_FAILED:STATUS_STARTING;
    packet.pid=(uint32_t)getpid();packet.source=STATUS_ADC;
    snprintf(packet.error,sizeof(packet.error),"%s",message);
    if(view->status_path)status_send(view->status_fd,view->status_path,&packet);
}

static void startup_adc_progress(unsigned hz,unsigned step,void *context)
{
    char message[64];
    snprintf(message,sizeof(message),"ADC CLOCK %.5g MHz",hz/1e6);
    fprintf(stderr,"Startup: %s\n",message);
    startup_show(context,message,40+step*8,0);
}

static void startup_pump_events(void)
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
        if(event.type==SDL_QUIT ||
           (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE) ||
           (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))stop_requested=1;
}

static int startup_usb_check(StartupView *view,int panel_required)
{
    uint64_t deadline=now_ms()+6000;
    int panel_ok=!panel_required;
    do {
        SerialControlMetrics metrics;
        if(panel_required){serial_control_metrics(&serial_controls,&metrics);panel_ok=metrics.connected;}
        if(panel_ok)return 1;
        startup_show(view,"USB PANEL LINK",10,0);
        startup_pump_events();
        scope_sleep_ms(100);
    } while(!stop_requested && now_ms()<deadline);
    snprintf(startup_error,sizeof(startup_error),"USB PANEL NO DATA");
    return 0;
}

static int startup_wait_ready(StartupView *view,const char *adc_device,int panel_required)
{
    const double gain[2]={0.004,0.004},zero[2]={128,128};
    char previous_error[sizeof(startup_error)]="";
    while(!stop_requested){
        int ok;
        uint64_t retry;
        *startup_error=0;
        startup_show(view,"USB",0,0);
        ok=startup_usb_check(view,panel_required);
        if(ok && require_data_device && !stop_requested){
            startup_show(view,"SD READ / WRITE",30,0);
            if(!linux_storage_check(application_dir,1)){
                snprintf(startup_error,sizeof(startup_error),"SD MOUNT / READ / WRITE ERROR");ok=0;}
        }
        if(ok && !stop_requested)ok=capture_selftest(adc_device,startup_adc_progress,view,startup_error,sizeof(startup_error));
        if(ok && !stop_requested){
            capture_pipeline=capture_pipeline_create_zynq(adc_device,gain,zero);
            if(!capture_pipeline){snprintf(startup_error,sizeof(startup_error),"ADC START AFTER SELF-TEST");ok=0;}
        }
        if(stop_requested)return 0;
        if(ok){
            demo_signal_begin_external(&demo);
            control_queue_reset(&controls);
            startup_show(view,"COMPLETE",100,0);
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
            return 1;
        }
        if(strcmp(previous_error,startup_error)){
            fprintf(stderr,"Startup waiting: %s\n",startup_error);
            snprintf(previous_error,sizeof(previous_error),"%s",startup_error);
        }
        retry=now_ms()+1000;
        do {
            startup_show(view,startup_error,0,1);
            startup_pump_events();
            scope_sleep_ms(100);
        } while(!stop_requested && now_ms()<retry);
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    struct sigaction stop_action={0};
    SDL_Window *lcd_window=NULL,*panel_window=NULL;SDL_Renderer *lcd_renderer=NULL,*panel_renderer=NULL;
    SDL_Texture *lcd_texture=NULL,*panel_texture=NULL;uint32_t lcd_id,panel_id;
    const char *snapshot_path=NULL,*serial_device=NULL,*raw_file=NULL,*remote_path=NULL,*adc_device=NULL;unsigned serial_baud=115200;
    RemoteControl *remote=NULL;
    const char *status_path=NULL;int status_fd=-1;uint64_t status_time=0;
    int panel_snapshot=0,raw_demo=0,diagnostics=0;uint64_t diagnostic_time=0,render_time=0,render_frames=0,frame_deadline=0;
    int running=1,windowed=0,kiosk=0,i,exit_status=0,worker_present=0;
    char default_settings[1024];const char *settings_path=NULL;int no_settings=0;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    const char *drm_device=NULL,*fb_device=NULL,*touch_device=NULL;unsigned touch_rotation=0;
    LinuxDisplay *drm_display=NULL;LinuxTouch *usb_touch=NULL;uint64_t touch_retry=0,touch_connections=0;
#endif
    screen_pixels=malloc((size_t)SCOPE_WIDTH*SCOPE_HEIGHT*4);
    panel_pixels=malloc((size_t)PANEL_WIDTH*PANEL_HEIGHT*4);
    if(!screen_pixels||!panel_pixels){fprintf(stderr,"Out of memory.\n");free(screen_pixels);free(panel_pixels);return 1;}
    determine_application_dir(argc?argv[0]:"scope_preview");demo_signal_init(&demo);
    if(settings_store_default_path(default_settings,sizeof(default_settings)))settings_path=default_settings;
    demo_signal_update_time(&demo,now_ms());
    for(i=1;i<argc;++i) {
        if(strcmp(argv[i],"--windowed")==0){windowed=1;kiosk=0;}
        else if(strcmp(argv[i],"--kiosk")==0){kiosk=1;windowed=0;}
        else if(strcmp(argv[i],"--serial")==0&&i+1<argc)serial_device=argv[++i];
        else if(strcmp(argv[i],"--baud")==0&&i+1<argc){if(!unsigned_option(argv[++i],&serial_baud)||!serial_baud)goto invalid_options;}
        else if(strcmp(argv[i],"--data-dir")==0&&i+1<argc){if(strlen(argv[++i])>=sizeof(application_dir)||!argv[i][0])goto invalid_options;strcpy(application_dir,argv[i]);}
        else if(strcmp(argv[i],"--settings-file")==0&&i+1<argc)settings_path=argv[++i];
        else if(strcmp(argv[i],"--no-settings")==0)no_settings=1;
        else if(strcmp(argv[i],"--require-data-device")==0)require_data_device=1;
        else if(strcmp(argv[i],"--raw-demo")==0)raw_demo=1;
#ifdef SCOPE_ENABLE_ZYNQ
        else if(strcmp(argv[i],"--adc")==0&&i+1<argc)adc_device=argv[++i];
#endif
        else if(strcmp(argv[i],"--capture")==0&&i+1<argc)raw_file=argv[++i];
        else if(strcmp(argv[i],"--diagnostics")==0)diagnostics=1;
        else if(strcmp(argv[i],"--remote-socket")==0&&i+1<argc)remote_path=argv[++i];
        else if(strcmp(argv[i],"--status-socket")==0&&i+1<argc)status_path=argv[++i];
        else if(strcmp(argv[i],"--console")==0&&i+1<argc)console_path=argv[++i];
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
        else if(strcmp(argv[i],"--drm")==0&&i+1<argc){drm_device=argv[++i];kiosk=1;windowed=0;}
        else if(strcmp(argv[i],"--fb")==0&&i+1<argc){fb_device=argv[++i];kiosk=1;windowed=0;}
        else if(strcmp(argv[i],"--touch")==0&&i+1<argc)touch_device=argv[++i];
        else if(strcmp(argv[i],"--touch-rotate")==0&&i+1<argc){if(!unsigned_option(argv[++i],&touch_rotation)||
            (touch_rotation!=0&&touch_rotation!=90&&touch_rotation!=180&&touch_rotation!=270))goto invalid_options;}
#endif
        else if((strcmp(argv[i],"--snapshot")==0||strcmp(argv[i],"--panel-snapshot")==0)&&i+1<argc){
            panel_snapshot=strcmp(argv[i],"--panel-snapshot")==0;
            snapshot_path=argv[++i];
        }
        else goto invalid_options;
    }
    if((raw_demo&&raw_file)||(adc_device&&(raw_demo||raw_file)))goto invalid_options;
    stop_action.sa_handler=request_stop;sigemptyset(&stop_action.sa_mask);
    sigaction(SIGINT,&stop_action,NULL);sigaction(SIGTERM,&stop_action,NULL);
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    if(drm_device&&fb_device)goto invalid_options;
    if(touch_device&&!drm_device&&!fb_device)goto invalid_options;
    if(drm_device||fb_device){kiosk=1;windowed=0;}
#endif
    if(snapshot_path){
        int result;panel_enabled=panel_snapshot;draw_all();result=panel_snapshot?
            save_bmp(snapshot_path,panel_pixels,PANEL_WIDTH,PANEL_HEIGHT):
            save_bmp(snapshot_path,screen_pixels,SCOPE_WIDTH,SCOPE_HEIGHT);
        free(screen_pixels);free(panel_pixels);return result?0:1;
    }
    panel_enabled=!kiosk;
    if(status_path){status_fd=status_sender_open();
        publish_service_status(status_fd,status_path,STATUS_STARTING,adc_device?STATUS_ADC:raw_file?STATUS_FILE:raw_demo?STATUS_RAW_DEMO:STATUS_DEMO,
                               serial_device!=NULL,-1,0,0);}
    {
        int available = console_path && linux_serial_get_baud(console_path,&console_baud);
        demo_signal_pc_status(&demo,available,console_baud,0);
    }
    control_queue_init(&controls);keyboard_controls.queue=&controls;
    if(serial_device&&!serial_control_start(&serial_controls,&controls,serial_device,serial_baud)){
        fprintf(stderr,"Cannot start serial transport\n");exit_status=1;goto cleanup;}
    if(serial_device)demo_signal_bind_scale_control(&demo,request_panel_scale,&serial_controls);
    if(raw_demo||raw_file){capture_pipeline=capture_pipeline_create(raw_file);if(!capture_pipeline){fprintf(stderr,"Cannot open raw capture source\n");exit_status=1;goto cleanup;}}
#ifdef SCOPE_ENABLE_ZYNQ
    /* Hardware acquisition starts only after the startup checks below. */
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
#endif
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    if(drm_device||fb_device)SDL_setenv("SDL_VIDEODRIVER","dummy",1);
#endif
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());exit_status=1;goto cleanup;}
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    if(drm_device||fb_device){drm_display=fb_device?
            linux_display_open_framebuffer(fb_device,SCOPE_WIDTH,SCOPE_HEIGHT):
            linux_display_open(drm_device,SCOPE_WIDTH,SCOPE_HEIGHT);
        if(!drm_display){fprintf(stderr,"Cannot open 1024x600 display: %s\n",fb_device?fb_device:drm_device);running=0;exit_status=1;}
        lcd_window=SDL_CreateWindow("Oscill input",0,0,SCOPE_WIDTH,SCOPE_HEIGHT,SDL_WINDOW_HIDDEN);}
    else
#endif
    if(kiosk) lcd_window=create_kiosk_window();
    else {
        int display_count=SDL_GetNumVideoDisplays(),display=-1;SDL_Rect bounds={0,0,0,0};
        if(!windowed)for(i=1;i<display_count;++i){SDL_Rect candidate;if(SDL_GetDisplayBounds(i,&candidate)==0&&
            candidate.w==SCOPE_WIDTH&&candidate.h==SCOPE_HEIGHT){display=i;bounds=candidate;break;}}
        if(display>=0)lcd_window=SDL_CreateWindow("Oscill LCD",bounds.x,bounds.y,SCOPE_WIDTH,SCOPE_HEIGHT,
                                                  SDL_WINDOW_BORDERLESS|SDL_WINDOW_SHOWN);
        else lcd_window=SDL_CreateWindow("Oscill LCD",SDL_WINDOWPOS_CENTERED_DISPLAY(0),SDL_WINDOWPOS_CENTERED_DISPLAY(0),
                                         SCOPE_WIDTH,SCOPE_HEIGHT,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    }
    if(!kiosk)panel_window=SDL_CreateWindow("Oscill controls",SDL_WINDOWPOS_CENTERED_DISPLAY(0),SDL_WINDOWPOS_CENTERED_DISPLAY(0),
                                           PANEL_WIDTH,PANEL_HEIGHT,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    if(!lcd_window||(!kiosk&&!panel_window)){fprintf(stderr,"Cannot create windows: %s\n",SDL_GetError());running=0;exit_status=1;}
    if(running){
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
        if(!drm_device&&!fb_device)
#endif
        lcd_renderer=make_renderer(lcd_window);if(panel_window)panel_renderer=make_renderer(panel_window);}
    if(lcd_renderer)lcd_texture=SDL_CreateTexture(lcd_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,SCOPE_WIDTH,SCOPE_HEIGHT);
    if(panel_renderer)panel_texture=SDL_CreateTexture(panel_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,PANEL_WIDTH,PANEL_HEIGHT);
    if(
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
       !drm_device&&!fb_device&&
#endif
       (!lcd_renderer||!lcd_texture||(!kiosk&&(!panel_renderer||!panel_texture)))){fprintf(stderr,"Cannot create renderers: %s\n",SDL_GetError());running=0;exit_status=1;}
    lcd_id=lcd_window?SDL_GetWindowID(lcd_window):0;panel_id=panel_window?SDL_GetWindowID(panel_window):0;
#ifdef SCOPE_ENABLE_ZYNQ
    if(running && adc_device){
        StartupView view={.status_fd=status_fd,.status_path=status_path,.renderer=lcd_renderer,.texture=lcd_texture};
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
        view.display=drm_display;
#endif
        if(!startup_wait_ready(&view,adc_device,serial_device!=NULL))goto cleanup;
    }
#endif
    if(!no_settings && settings_path){
        int loaded=settings_session_open(&settings,&demo,settings_path);
        if(loaded && serial_device)for(i=0;i<2;++i)restored_scale[i]=demo_signal_panel_scale(&demo,(unsigned)i);
        if(loaded && capture_pipeline && !demo.screen.running)restore_capture_pending=1;
    }
    demo.settings_available=settings.store!=NULL;demo_signal_refresh(&demo);
    draw_all();
    renderer=scope_renderer_create(sizeof(demo)+sizeof(browse_pixels),NULL);
    if(!renderer || !scope_renderer_enable_plot_layer(renderer,NULL)){fprintf(stderr,"Cannot start display renderer\n");exit_status=1;goto cleanup;}
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    if(drm_display){scope_renderer_set_output(renderer,present_display,drm_display);worker_present=1;}
#endif
    if(remote_path){remote=remote_control_open(remote_path,remote_command,NULL);
        if(!remote){perror("Remote socket");running=0;exit_status=1;}}
    while(running&&!stop_requested){SDL_Event event;uint64_t now;
        if(demo.screen.status_visible)invalidate();
        demo_signal_update_time(&demo,now_ms());
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
        if((drm_device||fb_device)&&touch_device){uint64_t input_now=now_ms();
            if(!usb_touch&&input_now>=touch_retry){usb_touch=linux_touch_open(touch_device,touch_rotation);touch_retry=input_now+500;if(usb_touch)++touch_connections;}
            if(usb_touch&&linux_touch_pump(usb_touch)<0){linux_touch_close(usb_touch);usb_touch=NULL;
                memset(contacts,0,sizeof(contacts));touch.active=pinch_block=0;}}
#endif
        /* Rendering/presenting is part of the frame budget, not an extra delay. */
        now=now_ms();
        int event_wait=2;
        if(SDL_WaitEventTimeout(&event,event_wait))do{
            demo_signal_update_time(&demo,now_ms());
            if(event.type==SDL_QUIT)running=0;
            else if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE)running=0;
            else if(event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_ESCAPE)running=0;
            else if(event.type==SDL_KEYDOWN)key_down(&event.key);
            else if(event.type==SDL_KEYUP)key_up(&event.key);
            else if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP)
                handle_finger(&event.tfinger,event.type);
            else if(event.type==SDL_MOUSEMOTION&&event.motion.which!=SDL_TOUCH_MOUSEID){int x=event.motion.x,y=event.motion.y;
                if(event.motion.windowID==panel_id){DemoControl next;logical_point(panel_window,PANEL_WIDTH,PANEL_HEIGHT,&x,&y);
                    next=panel_sdl_hit_test(x,y);if(next!=hovered){hovered=next;invalidate();}}
                else if(event.motion.windowID==lcd_id&&touch.active){logical_point(lcd_window,SCOPE_WIDTH,SCOPE_HEIGHT,&x,&y);touch_move(x,y);}}
            else if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT&&event.button.which!=SDL_TOUCH_MOUSEID){int x=event.button.x,y=event.button.y;
                if(event.button.windowID==panel_id){int adjustment;DemoControl control;logical_point(panel_window,PANEL_WIDTH,PANEL_HEIGHT,&x,&y);
                    adjustment=panel_sdl_hold_adjustment(x,y);if(adjustment){int value=(demo.hold_time_ms-200)/50+adjustment*2;
                        if(value<0)value=0;
                        if(value>26)value=26;
                        demo_signal_set_option(&demo,DEMO_MENU_DEBUG,1,value);invalidate();}
                    else{control=panel_sdl_hit_test(x,y);if(control!=DEMO_CONTROL_COUNT&&!mouse_hold.active){begin_press(&mouse_hold,control,0,now_ms());SDL_CaptureMouse(SDL_TRUE);invalidate();}}}
                else if(event.button.windowID==lcd_id){logical_point(lcd_window,SCOPE_WIDTH,SCOPE_HEIGHT,&x,&y);lcd_touch_begin(x,y);if(touch.active)SDL_CaptureMouse(SDL_TRUE);}}
            else if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT&&event.button.which!=SDL_TOUCH_MOUSEID){int x=event.button.x,y=event.button.y;
                if(event.button.windowID==panel_id||mouse_hold.active)finish_press(&mouse_hold,now_ms());
                if(event.button.windowID==lcd_id&&touch.active){logical_point(lcd_window,SCOPE_WIDTH,SCOPE_HEIGHT,&x,&y);lcd_touch_end(y);}SDL_CaptureMouse(SDL_FALSE);invalidate();}
            else if(event.type==SDL_MOUSEWHEEL&&event.wheel.windowID==panel_id){int x,y;SDL_GetMouseState(&x,&y);logical_point(panel_window,PANEL_WIDTH,PANEL_HEIGHT,&x,&y);
                {DemoControl control=panel_sdl_hit_test(x,y);if(panel_sdl_is_encoder(control)){wheel_remainder+=event.wheel.y;demo_signal_rotate(&demo,control,wheel_remainder);wheel_remainder=0;invalidate();}}}
        }while(SDL_PollEvent(&event));
        now=now_ms();if(pending_chord!=DEMO_CONTROL_COUNT&&now-pending_time>CHORD_WINDOW_MS)pending_chord=DEMO_CONTROL_COUNT;
        drain_controls(now);fire_long_press(&mouse_hold,now);
        for(i=0;i<CONTROL_ID_COUNT;++i)fire_long_press(&key_holds[i],now);
        fire_lcd_touch_hold(now);
        update_pc_console(now);
        if(serial_device){
            SerialControlMetrics panel;
            uint64_t panel_now=scope_clock_ns()/1000000;
            serial_control_set_run(&serial_controls,demo.screen.running);
            for(i=0;i<2;++i)serial_control_set_encoder_mode(&serial_controls,(unsigned)i,
                demo.position_mode[i]||demo_signal_panel_scale(live_settings(),(unsigned)i)==CONTROL_SCALE_OFF||
                demo.waveform_loaded||demo_signal_menu_encoder(&demo,(DemoControl)i));
            serial_control_metrics(&serial_controls,&panel);
            sync_panel_ranges(&panel,panel_now);
            if(update_panel_battery(&panel,panel_now))invalidate();
            if(!demo.waveform_loaded)for(i=0;i<2;++i)if((panel.scale_valid&(1U<<i))&&
                restored_scale[i]<0 &&
                demo_signal_panel_scale(&demo,(unsigned)i)!=panel.scale_mv[i]){
                demo_signal_accept_scale(&demo,(unsigned)i,panel.scale_mv[i]);invalidate();}
        }
        if(capture_pipeline){CaptureRequest request;CaptureMetrics metrics;
            if(adc_device)capture_pipeline_presented(capture_pipeline,scope_renderer_presented_plot(renderer));
            int restoring=restored_scale[0]>0 || restored_scale[1]>0;
            if(!restoring && restore_capture_pending){demo_signal_force_trigger(&demo);restore_capture_pending=0;}
            capture_request_from_demo(&demo,&request);
            if(restoring || demo.waveform_loaded||demo.screen.browser_visible)request.running=0;
            capture_pipeline_request(capture_pipeline,&request);
            const DisplayFrame *next_frame=capture_pipeline_acquire(capture_pipeline);
            if(next_frame){leased_frame=next_frame;capture_frame_valid=1;
                if(raw_file)demo.screen.running=0;
                if(adc_device&&demo.trigger_mode_index==2)single_capture_id=next_frame->capture_id;
                dirty=plot_dirty=1;}
            capture_pipeline_metrics(capture_pipeline,&metrics);
            if(demo_signal_set_adc_level(&demo,metrics.adc_code,
                metrics.adc_valid&&metrics.adc_age_ms<=STATUS_HEARTBEAT_MS)&&
                demo.screen.menu_open&&demo.menu_kind==DEMO_MENU_DEBUG)invalidate();
            if(adc_device){
                uint64_t presented=scope_renderer_presented_plot(renderer);
                if(single_capture_id && demo.trigger_mode_index==2 && presented>=single_capture_id){
                    single_capture_id=0;demo.screen.running=0;invalidate();
                }else if(demo.trigger_mode_index!=2)single_capture_id=0;
                /* Accepted source record -> RUN; actual plot presentation -> HOLD. */
                int waiting=capture_waiting(&metrics,presented,demo.screen.running,demo.trigger_mode_index);
                if(demo.screen.waiting_for_trigger!=waiting){demo.screen.waiting_for_trigger=waiting;dirty=overlay_dirty=1;}
            }
            if(metrics.source_error&&demo.screen.running){demo.screen.running=0;demo_signal_notify(&demo,"ADC CAPTURE ERROR");invalidate();}}
        if(demo.screen.running&&!demo.external_capture){demo_signal_advance(&demo);demo.screen.plot_id=demo.capture_sequence;invalidate();}
        if(!demo.external_capture && demo.screen.running && demo.trigger_mode_index!=0){
            int waiting=demo.capture_sequence<=scope_renderer_presented_plot(renderer);
            if(demo.screen.waiting_for_trigger!=waiting){demo.screen.waiting_for_trigger=waiting;dirty=overlay_dirty=1;}
        }
        if(settings_session_poll(&settings,live_settings()))invalidate();
        if(demo.response_active)invalidate();
        if(demo.screen.status_visible||mouse_hold.active||key_holds[0].active||key_holds[1].active)invalidate();
        if(scope_renderer_poll_plot(renderer))dirty=overlay_dirty=1;
        if(dirty&&running && now_ms()>=frame_deadline){frame_deadline=now_ms()+16;draw_all();dirty=0;}
        if(worker_present){
            scope_renderer_metrics(renderer,&render_frames,&render_time,NULL);
            if(scope_renderer_failed(renderer)){running=0;exit_status=1;}
        }else if(scope_renderer_take(renderer,screen_pixels)){
            scope_renderer_metrics(renderer,&render_frames,&render_time,NULL);
            {SDL_UpdateTexture(lcd_texture,NULL,screen_pixels,SCOPE_WIDTH*4);SDL_RenderClear(lcd_renderer);SDL_RenderCopy(lcd_renderer,lcd_texture,NULL,NULL);SDL_RenderPresent(lcd_renderer);
            scope_renderer_acknowledge(renderer);
            if(panel_texture){SDL_UpdateTexture(panel_texture,NULL,panel_pixels,PANEL_WIDTH*4);SDL_RenderClear(panel_renderer);SDL_RenderCopy(panel_renderer,panel_texture,NULL,NULL);SDL_RenderPresent(panel_renderer);}dirty=0;}}
        remote_control_pump(remote);
        if(status_path && (!status_time || now-status_time>=1000)){
            int touch_status=-1;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
            if(touch_device)touch_status=usb_touch!=NULL;
#endif
            publish_service_status(status_fd,status_path,STATUS_READY,
                (raw_file||demo.waveform_loaded)?STATUS_FILE:adc_device?STATUS_ADC:raw_demo?STATUS_RAW_DEMO:STATUS_DEMO,
                serial_device!=NULL,touch_status,render_frames,render_time);
            status_time=now;
        }
        if(diagnostics&&now-diagnostic_time>=1000){CaptureMetrics m={0};SerialControlMetrics uart={0};if(capture_pipeline)capture_pipeline_metrics(capture_pipeline,&m);
            serial_control_metrics(&serial_controls,&uart);
            fprintf(stderr,"render_ms=%.3f frames=%llu processing_ms=%.3f blocks=%llu dropped=%llu overwritten=%llu occupancy=%zu source_error=%d waveforms=%llu\n",
                render_frames?render_time/1e6/render_frames:0,(unsigned long long)render_frames,m.processing_ns/1e6,
                (unsigned long long)m.blocks,(unsigned long long)m.dropped,(unsigned long long)m.overwritten,m.occupancy,m.source_error,(unsigned long long)m.waveforms);
            if(serial_device)fprintf(stderr,"uart_link=%d uart_device=%s packets=%llu crc_errors=%llu format_errors=%llu gaps=%llu duplicates=%llu read_errors=%llu reconnects=%llu\n",
                uart.connected,uart.device[0]?uart.device:"none",(unsigned long long)uart.packets,(unsigned long long)uart.crc_errors,(unsigned long long)uart.format_errors,
                (unsigned long long)uart.sequence_gaps,(unsigned long long)uart.duplicates,(unsigned long long)uart.read_errors,(unsigned long long)uart.reconnects);
            if(serial_device&&uart.power_received_ms){uint64_t uart_now=scope_clock_ns()/1000000;
                fprintf(stderr,"panel_power_mv=%d age_ms=%llu uart_write_errors=%llu\n",
                    uart.power_mv,(unsigned long long)(uart_now>=uart.power_received_ms?uart_now-uart.power_received_ms:0),
                    (unsigned long long)uart.write_errors);}
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
            if(touch_device){LinuxTouchStats stats;linux_touch_stats(usb_touch,&stats);
                fprintf(stderr,"touch_connected=%d connections=%llu reports=%llu events=%llu overflows=%llu\n",
                    usb_touch!=NULL,(unsigned long long)touch_connections,(unsigned long long)stats.reports,
                    (unsigned long long)stats.events,(unsigned long long)stats.overflows);}
#endif
            diagnostic_time=now;}
    }
cleanup:
    scope_renderer_destroy(renderer);renderer=NULL;
    settings_session_close(&settings,live_settings());
    if(status_path){publish_service_status(status_fd,status_path,exit_status?STATUS_FAILED:STATUS_STOPPED,
        adc_device?STATUS_ADC:raw_file?STATUS_FILE:raw_demo?STATUS_RAW_DEMO:STATUS_DEMO,serial_device!=NULL,-1,
        render_frames,render_time);if(status_fd>=0)close(status_fd);}
    remote_control_close(remote);
    capture_pipeline_destroy(capture_pipeline);serial_control_stop(&serial_controls);control_queue_destroy(&controls);
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    linux_touch_close(usb_touch);linux_display_close(drm_display);
#endif
    if(panel_texture)SDL_DestroyTexture(panel_texture);
    if(lcd_texture)SDL_DestroyTexture(lcd_texture);
    if(panel_renderer)SDL_DestroyRenderer(panel_renderer);
    if(lcd_renderer)SDL_DestroyRenderer(lcd_renderer);
    if(panel_window)SDL_DestroyWindow(panel_window);
    if(lcd_window)SDL_DestroyWindow(lcd_window);
    SDL_Quit();
    free(panel_pixels);free(screen_pixels);return exit_status;
invalid_options:
    fprintf(stderr,"Invalid option or argument. Use --windowed, --kiosk, --serial DEVICE, --baud RATE, --console TTY, --data-dir DIR, --require-data-device, --adc DEVICE (Zynq build), --raw-demo, --capture FILE, --diagnostics, --remote-socket PATH, --status-socket PATH, --snapshot BMP or --panel-snapshot BMP.\n");
    free(panel_pixels);free(screen_pixels);return 2;
}
