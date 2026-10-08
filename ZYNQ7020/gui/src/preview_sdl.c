#include "panel_sdl.h"
#include "wave_file.h"
#include "linux_storage.h"
#include "capture_adapter.h"
#include "control_transport.h"
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
#include "linux_display.h"
#include "linux_touch.h"
#endif

#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
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
static int dirty = 1;
static int panel_enabled = 1;
static ControlQueue controls;
static KeyboardControlTransport keyboard_controls;
static SerialControlTransport serial_controls;
static CapturePipeline *capture_pipeline;
static DisplayFrame capture_frame;
static int capture_frame_valid;

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
static int hold_time_ms = 300;

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
    int fine_x_remainder, fine_y_remainder, zoom_pan_remainder;
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
static void invalidate(void) { dirty = 1; }

static int has_hold_action(DemoControl control)
{
    return control != DEMO_CONTROL_COUNT &&
           control != DEMO_BTN_CH1 && control != DEMO_BTN_CH2;
}

static DemoControl active_control(int *progress)
{
    const HeldControl *held = mouse_hold.active ? &mouse_hold : NULL;int i;
    for(i=0;i<CONTROL_ID_COUNT&&!held;++i)if(key_holds[i].active)held=&key_holds[i];
    *progress = -1;
    if (!held) return DEMO_CONTROL_COUNT;
    if (!held->suppressed && has_hold_action(held->control)) {
        uint64_t elapsed = now_ms() - held->since;
        *progress = held->long_done || elapsed >= (uint64_t)hold_time_ms ? 100 :
                    (int)(elapsed * 100 / (uint64_t)hold_time_ms);
    }
    return held->control;
}

static void draw_all(void)
{
    int progress;
    DemoControl active = active_control(&progress);
    if(capture_pipeline&&capture_frame_valid&&!demo.waveform_loaded&&!demo.screen.browser_visible)
        capture_display_apply(&demo,&capture_frame);
    else {
        demo.screen.ch1_min_samples=demo.minimum[0];
        demo.screen.ch1_max_samples=demo.maximum[0];
        demo.screen.ch2_min_samples=demo.minimum[1];
        demo.screen.ch2_max_samples=demo.maximum[1];
    }
    scope_screen_render(screen_pixels, SCOPE_WIDTH, &demo.screen);
    if(panel_enabled)
        panel_sdl_draw(panel_pixels, &demo, active, hovered, progress, hold_time_ms);
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

static int open_browse_capture(void)
{
    const char *name=demo_signal_ui_browse_filename(&demo); char path[PATH_MAX]; size_t length;
    if(!name || !make_path(path,sizeof(path),name)) return 0;
    length=strlen(name);
    if(length>=4 && strcasecmp(name+length-4,".bmp")==0) {
        if(!load_browse_bmp(path)) return 0;
        if(live_snapshot_valid) {
            int selected=demo.browse_selected; demo=live_before_browse;
            demo.browse_selected=selected; live_snapshot_valid=0;
        }
        demo_signal_ui_show_capture(&demo,demo.browse_files[demo.browse_selected],browse_pixels);
    } else {
        if(!load_browse_wave(path)) return 0;
        if(live_snapshot_valid) {
            int selected=demo.browse_selected; demo=live_before_browse;
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
            scope_screen_render(screen_pixels,SCOPE_WIDTH,&demo.screen);
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
    if(action==DEMO_ACTION_BROWSE_REFRESH) refresh_browse();
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
            demo=live_before_browse; demo.browse_selected=selected; live_snapshot_valid=0;
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
                         now-held->since>=(uint64_t)hold_time_ms,now);
    held->active=0;
}

static void fire_long_press(HeldControl *held, uint64_t now)
{
    if(!held->active||held->suppressed||held->long_done||!has_hold_action(held->control)||
       now-held->since<(uint64_t)hold_time_ms) return;
    held->long_done=1; pending_chord=DEMO_CONTROL_COUNT;
    press_control_at(held->control,1,now);
}

static int touch_view_upper(void)
{
    return demo.screen.split_height;
}

static int touch_screen_y(int logical_y)
{
    if(demo.screen.zoom_enabled||demo.screen.fft_enabled) {
        int upper=touch_view_upper();
        return SCOPE_PLOT_Y+upper+logical_y*(SCOPE_PLOT_HEIGHT-upper)/SCOPE_PLOT_HEIGHT;
    }
    return SCOPE_PLOT_Y+logical_y;
}

static int touch_plot_y(int screen_y)
{
    if(demo.screen.zoom_enabled||demo.screen.fft_enabled) {
        int upper=touch_view_upper();
        return (screen_y-SCOPE_PLOT_Y-upper)*SCOPE_PLOT_HEIGHT/(SCOPE_PLOT_HEIGHT-upper);
    }
    return screen_y-SCOPE_PLOT_Y;
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
        return *row<demo.screen.menu_count?TOUCH_MENU_ROW:TOUCH_NONE;
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
        if((demo.screen.zoom_enabled||demo.screen.fft_enabled)&&y<SCOPE_PLOT_Y+demo.screen.split_height)
            return demo.screen.fft_cursor_visible?TOUCH_FFT_CURSOR:TOUCH_NONE;
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
        case TOUCH_TOP_MENU: demo_signal_ui_open_menu(&demo,DEMO_MENU_MAIN); break;
        case TOUCH_BROWSER_BACK: demo_signal_ui_close_capture(&demo); break;
        case TOUCH_BROWSER_PREV: action=DEMO_ACTION_BROWSE_PREV; break;
        case TOUCH_BROWSER_NEXT: action=DEMO_ACTION_BROWSE_NEXT; break;
        case TOUCH_MENU_BACK: demo_signal_ui_menu_back(&demo); break;
        case TOUCH_MEASURE_CLEAR: demo_signal_ui_menu_select(&demo,SCOPE_MEASURE_CATALOG_ITEMS); demo_signal_ui_menu_activate(&demo); break;
        case TOUCH_MEASURE_HIDE: demo_signal_ui_toggle_measurements_visible(&demo); break;
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
    if(demo.screen.zoom_enabled&&!demo.waveform_loaded) {
        int total=touch.zoom_pan_remainder+delta;
        int source_delta=total/demo.zoom_factor;
        touch.zoom_pan_remainder=total%demo.zoom_factor;delta=source_delta*demo.zoom_factor;
    }
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
    if(!touch.active||touch.moved||touch.long_done||now-touch.since<(uint64_t)hold_time_ms)return;
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
    touch.fine_x_remainder=touch.fine_y_remainder=touch.zoom_pan_remainder=0;
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

static void drain_controls(uint64_t now)
{
    static unsigned generation;static uint32_t down_ms[CONTROL_ID_COUNT];
    ControlEvent event;unsigned next=control_queue_generation(&controls);
    if(next!=generation){memset(key_holds,0,sizeof(key_holds));pending_chord=DEMO_CONTROL_COUNT;generation=next;}
    while(control_queue_pop(&controls,&event)){
        if(event.control>=CONTROL_ID_COUNT)continue;
        if(event.type==CONTROL_ROTATE){pending_chord=DEMO_CONTROL_COUNT;demo_signal_rotate(&demo,(DemoControl)event.control,event.value);}
        else if(event.type==CONTROL_DOWN&&!key_holds[event.control].active){down_ms[event.control]=event.timestamp_ms;
            begin_press(&key_holds[event.control],(DemoControl)event.control,0,now);}
        else if(event.type==CONTROL_UP&&key_holds[event.control].active){
            /* Source duration survives UART burst delivery and uint32 timestamp wrap. */
            uint32_t duration=event.timestamp_ms-down_ms[event.control];
            if(duration>=((uint32_t)hold_time_ms)&&has_hold_action((DemoControl)event.control)&&
                !key_holds[event.control].long_done&&!key_holds[event.control].suppressed){
                key_holds[event.control].long_done=1;press_control_at((DemoControl)event.control,1,now);}
            finish_press(&key_holds[event.control],now);
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
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
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

int main(int argc, char **argv)
{
    struct sigaction stop_action={0};
    SDL_Window *lcd_window=NULL,*panel_window=NULL;SDL_Renderer *lcd_renderer=NULL,*panel_renderer=NULL;
    SDL_Texture *lcd_texture=NULL,*panel_texture=NULL;uint32_t lcd_id,panel_id;
    const char *snapshot_path=NULL,*serial_device=NULL,*raw_file=NULL;unsigned serial_baud=115200;
    int panel_snapshot=0,raw_demo=0,diagnostics=0;uint64_t diagnostic_time=0,render_time=0,render_frames=0;
    int running=1,windowed=0,kiosk=0,i,exit_status=0;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
    const char *drm_device=NULL,*fb_device=NULL,*touch_device=NULL;unsigned touch_rotation=0;
    LinuxDisplay *drm_display=NULL;LinuxTouch *usb_touch=NULL;uint64_t touch_retry=0,touch_connections=0;
#endif
    screen_pixels=malloc((size_t)SCOPE_WIDTH*SCOPE_HEIGHT*4);
    panel_pixels=malloc((size_t)PANEL_WIDTH*PANEL_HEIGHT*4);
    if(!screen_pixels||!panel_pixels){fprintf(stderr,"Out of memory.\n");free(screen_pixels);free(panel_pixels);return 1;}
    determine_application_dir(argc?argv[0]:"scope_preview");demo_signal_init(&demo);
    for(i=1;i<argc;++i) {
        if(strcmp(argv[i],"--windowed")==0){windowed=1;kiosk=0;}
        else if(strcmp(argv[i],"--kiosk")==0){kiosk=1;windowed=0;}
        else if(strcmp(argv[i],"--serial")==0&&i+1<argc)serial_device=argv[++i];
        else if(strcmp(argv[i],"--baud")==0&&i+1<argc){if(!unsigned_option(argv[++i],&serial_baud)||!serial_baud)goto invalid_options;}
        else if(strcmp(argv[i],"--data-dir")==0&&i+1<argc){if(strlen(argv[++i])>=sizeof(application_dir)||!argv[i][0])goto invalid_options;strcpy(application_dir,argv[i]);}
        else if(strcmp(argv[i],"--require-data-device")==0)require_data_device=1;
        else if(strcmp(argv[i],"--raw-demo")==0)raw_demo=1;
        else if(strcmp(argv[i],"--capture")==0&&i+1<argc)raw_file=argv[++i];
        else if(strcmp(argv[i],"--diagnostics")==0)diagnostics=1;
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
    if(raw_demo&&raw_file)goto invalid_options;
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
    control_queue_init(&controls);keyboard_controls.queue=&controls;
    if(serial_device&&!serial_control_start(&serial_controls,&controls,serial_device,serial_baud)){
        fprintf(stderr,"Cannot start serial transport\n");exit_status=1;goto cleanup;}
    if(raw_demo||raw_file){capture_pipeline=capture_pipeline_create(raw_file);if(!capture_pipeline){fprintf(stderr,"Cannot open raw capture source\n");exit_status=1;goto cleanup;}}
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
    while(running&&!stop_requested){SDL_Event event;uint64_t now;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
        if((drm_device||fb_device)&&touch_device){uint64_t input_now=now_ms();
            if(!usb_touch&&input_now>=touch_retry){usb_touch=linux_touch_open(touch_device,touch_rotation);touch_retry=input_now+500;if(usb_touch)++touch_connections;}
            if(usb_touch&&linux_touch_pump(usb_touch)<0){linux_touch_close(usb_touch);usb_touch=NULL;
                memset(contacts,0,sizeof(contacts));touch.active=pinch_block=0;}}
#endif
        if(SDL_WaitEventTimeout(&event,16))do{
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
                    adjustment=panel_sdl_hold_adjustment(x,y);if(adjustment){hold_time_ms+=adjustment*100;if(hold_time_ms<200)hold_time_ms=200;if(hold_time_ms>1500)hold_time_ms=1500;invalidate();}
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
        if(capture_pipeline){CaptureRequest request;capture_request_from_demo(&demo,&request);
            capture_pipeline_request(capture_pipeline,&request);
            if(capture_pipeline_frame(capture_pipeline,&capture_frame)){capture_frame_valid=1;if(raw_file)demo.screen.running=0;invalidate();}}
        if(demo.screen.running||demo.notice_ticks>0||mouse_hold.active||key_holds[0].active||key_holds[1].active){demo_signal_advance(&demo);invalidate();}
        if(dirty&&running){uint64_t render_begin=scope_clock_ns();draw_all();render_time+=scope_clock_ns()-render_begin;++render_frames;
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
            if(drm_display){int submitted=linux_display_present(drm_display,screen_pixels);if(submitted<0){running=0;exit_status=1;}else dirty=!submitted;}
            else
#endif
            {SDL_UpdateTexture(lcd_texture,NULL,screen_pixels,SCOPE_WIDTH*4);SDL_RenderClear(lcd_renderer);SDL_RenderCopy(lcd_renderer,lcd_texture,NULL,NULL);SDL_RenderPresent(lcd_renderer);
            if(panel_texture){SDL_UpdateTexture(panel_texture,NULL,panel_pixels,PANEL_WIDTH*4);SDL_RenderClear(panel_renderer);SDL_RenderCopy(panel_renderer,panel_texture,NULL,NULL);SDL_RenderPresent(panel_renderer);}dirty=0;}}
        if(diagnostics&&now-diagnostic_time>=1000){CaptureMetrics m={0};SerialControlMetrics uart={0};if(capture_pipeline)capture_pipeline_metrics(capture_pipeline,&m);
            serial_control_metrics(&serial_controls,&uart);
            fprintf(stderr,"render_ms=%.3f frames=%llu processing_ms=%.3f blocks=%llu dropped=%llu overwritten=%llu occupancy=%zu source_error=%d\n",
                render_frames?render_time/1e6/render_frames:0,(unsigned long long)render_frames,m.processing_ns/1e6,
                (unsigned long long)m.blocks,(unsigned long long)m.dropped,(unsigned long long)m.overwritten,m.occupancy,m.source_error);
            if(serial_device)fprintf(stderr,"uart_link=%d packets=%llu crc_errors=%llu format_errors=%llu gaps=%llu duplicates=%llu read_errors=%llu reconnects=%llu\n",
                uart.connected,(unsigned long long)uart.packets,(unsigned long long)uart.crc_errors,(unsigned long long)uart.format_errors,
                (unsigned long long)uart.sequence_gaps,(unsigned long long)uart.duplicates,(unsigned long long)uart.read_errors,(unsigned long long)uart.reconnects);
#ifdef SCOPE_ENABLE_LINUX_DISPLAY
            if(touch_device){LinuxTouchStats stats;linux_touch_stats(usb_touch,&stats);
                fprintf(stderr,"touch_connected=%d connections=%llu reports=%llu events=%llu overflows=%llu\n",
                    usb_touch!=NULL,(unsigned long long)touch_connections,(unsigned long long)stats.reports,
                    (unsigned long long)stats.events,(unsigned long long)stats.overflows);}
#endif
            diagnostic_time=now;}
    }
cleanup:
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
    fprintf(stderr,"Invalid option or argument. Use --windowed, --kiosk, --serial DEVICE, --baud RATE, --data-dir DIR, --require-data-device, --raw-demo, --capture FILE, --diagnostics, --snapshot BMP or --panel-snapshot BMP.\n");
    free(panel_pixels);free(screen_pixels);return 2;
}
