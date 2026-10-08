#include "panel_win32.h"
#include "wave_file.h"
#include "boot_splash.h"

#include <shellapi.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static DemoSignal demo;
static ULONGLONG splash_until;

static void render_lcd(uint32_t *pixels)
{
    if (GetTickCount64() < splash_until)
        boot_splash_render(pixels, SCOPE_WIDTH, SCOPE_WIDTH, SCOPE_HEIGHT);
    else
        scope_screen_render(pixels, SCOPE_WIDTH, &demo.screen);
}
static HBITMAP screen_bitmap;
static HBITMAP panel_bitmap;
static HDC screen_dc;
static HDC panel_dc;
static HGDIOBJ old_screen_bitmap;
static HGDIOBJ old_panel_bitmap;
static uint32_t *screen_pixels;
static uint32_t *panel_pixels;
static uint32_t browse_pixels[SCOPE_WIDTH * SCOPE_HEIGHT];
static DemoWaveCapture browse_wave;
static DemoSignal live_before_browse;
static int live_snapshot_valid;
static char browse_names[DEMO_BROWSE_FILES][DEMO_BROWSE_NAME];
static HWND lcd_window;
static HWND controls_window;
static DemoControl hovered = DEMO_CONTROL_COUNT;
typedef struct {
    DemoControl control;
    WPARAM key_code;
    ULONGLONG since;
    int active;
    int long_done;
    int suppressed;
} HeldControl;

static HeldControl mouse_hold;
static HeldControl key_holds[2];
static int wheel_remainder;
static DemoControl pending_chord = DEMO_CONTROL_COUNT;
static ULONGLONG pending_time;
static DemoSignal chord_snapshot;
typedef enum {
    TOUCH_NONE, TOUCH_TOP_MENU, TOUCH_TOP_SCREENSHOT, TOUCH_TOP_WAVE,
    TOUCH_TOP_FINE, TOUCH_TOP_PROCESSING, TOUCH_TOP_RUN,
    TOUCH_CH1, TOUCH_CH2,
    TOUCH_TIME, TOUCH_TRIGGER_SOURCE, TOUCH_BOTTOM_CURSOR,
    TOUCH_MENU_BACK, TOUCH_MENU_DRAG, TOUCH_MENU_OUTSIDE, TOUCH_MENU_ROW,
    TOUCH_MEASURE_HIDE, TOUCH_MEASURE_CLEAR,
    TOUCH_MEASURE_CANCEL, TOUCH_MEASURE_CONFIRM,
    TOUCH_BROWSE_DELETE_CANCEL, TOUCH_BROWSE_DELETE_CONFIRM,
    TOUCH_PLOT_TRIGGER, TOUCH_PLOT_CURSOR,
    TOUCH_MEASURE_DRAG, TOUCH_MEASURE_LAYOUT, TOUCH_CURSOR_STRIP,
    TOUCH_ZOOM_OVERVIEW, TOUCH_PLOT_CH1, TOUCH_PLOT_CH2,
    TOUCH_TIME_IN, TOUCH_TIME_OUT, TOUCH_BROWSER_PREV, TOUCH_BROWSER_NEXT,
    TOUCH_BROWSER_BACK, TOUCH_SPLIT, TOUCH_FFT_CURSOR
} TouchZone;
typedef struct {
    TouchZone zone;
    int row;
    int x;
    int y;
    int last_x;
    int last_y;
    int remainder;
    int fine_x_remainder;
    int fine_y_remainder;
    int zoom_pan_remainder;
    int axis;
    int active;
    int moved;
    ULONGLONG since;
    int long_done;
} TouchInput;
static TouchInput touch;
typedef struct {
    UINT32 id;
    POINT point;
    int active;
} TouchContact;
static TouchContact contacts[2];
static unsigned long long pinch_start_sq;
static int pinch_consumed;
static int pinch_block;

typedef struct {
    RECT primary_work;
    RECT lcd_bounds;
    int primary_found;
    int lcd_found;
} DisplayLayout;

#define CHORD_WINDOW_MS 90
static int hold_time_ms = 300;

static BOOL CALLBACK find_display(HMONITOR monitor, HDC dc, LPRECT bounds, LPARAM context)
{
    DisplayLayout *layout = (DisplayLayout *)context;
    MONITORINFOEXW info = {0};
    (void)dc;
    (void)bounds;
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) return TRUE;
    if (info.dwFlags & MONITORINFOF_PRIMARY) {
        layout->primary_work = info.rcWork;
        layout->primary_found = 1;
    } else if (!layout->lcd_found &&
               info.rcMonitor.right - info.rcMonitor.left == SCOPE_WIDTH &&
               info.rcMonitor.bottom - info.rcMonitor.top == SCOPE_HEIGHT) {
        layout->lcd_bounds = info.rcMonitor;
        layout->lcd_found = 1;
    }
    return TRUE;
}

static void invalidate_windows(void)
{
    if (lcd_window) InvalidateRect(lcd_window, NULL, FALSE);
    if (controls_window) InvalidateRect(controls_window, NULL, FALSE);
}

static int has_hold_action(DemoControl control)
{
    return control != DEMO_CONTROL_COUNT &&
           control != DEMO_BTN_CH1 && control != DEMO_BTN_CH2;
}

static void draw_all(void)
{
    const HeldControl *held = mouse_hold.active ? &mouse_hold :
                              key_holds[0].active ? &key_holds[0] :
                              key_holds[1].active ? &key_holds[1] : NULL;
    DemoControl active = held ? held->control : DEMO_CONTROL_COUNT;
    int progress = -1;
    if (held && !held->suppressed && has_hold_action(active)) {
        ULONGLONG elapsed = GetTickCount64() - held->since;
        progress = held->long_done || elapsed >= (ULONGLONG)hold_time_ms ? 100 :
                   (int)(elapsed * 100 / hold_time_ms);
    }
    render_lcd(screen_pixels);
    panel_draw(panel_dc, &demo, active, hovered, progress, hold_time_ms);
}

static int save_bmp(const wchar_t *path, const uint32_t *pixels, int width, int height)
{
    BITMAPFILEHEADER file_header = {0};
    BITMAPINFOHEADER info_header = {0};
    FILE *file = _wfopen(path, L"wb");
    int y;
    if (!file) return 0;
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info_header);
    file_header.bfSize = file_header.bfOffBits + width * height * 4;
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = width;
    info_header.biHeight = height;
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    if (fwrite(&file_header, sizeof(file_header), 1, file) != 1 ||
        fwrite(&info_header, sizeof(info_header), 1, file) != 1) {
        fclose(file);
        return 0;
    }
    for (y = height - 1; y >= 0; --y)
        if (fwrite(pixels + (size_t)y * width, 4, (size_t)width, file) != (size_t)width) {
            fclose(file);
            return 0;
        }
    return fclose(file) == 0;
}

static int save_wave(const wchar_t *path)
{
    FILE *file = _wfopen(path, L"w");
    DemoWaveCapture wave;
    int ok;
    if (!file) return 0;
    demo_signal_export_wave(&demo, &wave);
    ok = wave_file_write(file, &wave);
    return fclose(file) == 0 && ok;
}

static int capture_directory(wchar_t *path)
{
    wchar_t *separator;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return 0;
    separator = wcsrchr(path, L'\\');
    if (!separator) return 0;
    *separator = L'\0';
    return 1;
}

static int compare_browse_names(const void *left, const void *right)
{
    return strcmp((const char *)right, (const char *)left);
}

static void refresh_browse(void)
{
    static const wchar_t *patterns[] = {L"capture_*.bmp", L"capture_*.csv"};
    wchar_t directory[MAX_PATH], pattern[MAX_PATH];
    const char *names[DEMO_BROWSE_FILES];
    int count = 0, i;
    if (capture_directory(directory)) {
        WIN32_FIND_DATAW entry;
        HANDLE search;
        if (swprintf(pattern, MAX_PATH, L"%ls\\%ls", directory,
                     patterns[demo.browse_filter]) < 0) return;
        search = FindFirstFileW(pattern, &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    count < DEMO_BROWSE_FILES) {
                    size_t length = wcstombs(browse_names[count], entry.cFileName,
                                             DEMO_BROWSE_NAME - 1);
                    if (length != (size_t)-1) {
                        browse_names[count][length] = '\0';
                        ++count;
                    }
                }
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
    }
    qsort(browse_names, (size_t)count, sizeof(browse_names[0]), compare_browse_names);
    for (i = 0; i < count; ++i) names[i] = browse_names[i];
    demo_signal_ui_set_browse_files(&demo, names, count);
}

static int delete_browse_capture(void)
{
    const char *name = demo_signal_ui_browse_filename(&demo);
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    if (!name || !capture_directory(directory) ||
        swprintf(path, MAX_PATH, L"%ls\\%hs", directory, name) < 0) return 0;
    return DeleteFileW(path) != 0;
}

static int load_browse_bmp(const wchar_t *path)
{
    BITMAPFILEHEADER file_header;
    BITMAPINFOHEADER info_header;
    FILE *file = _wfopen(path, L"rb");
    int y;
    if (!file) return 0;
    if (fread(&file_header, sizeof(file_header), 1, file) != 1 ||
        fread(&info_header, sizeof(info_header), 1, file) != 1 ||
        file_header.bfType != 0x4d42 || info_header.biWidth != SCOPE_WIDTH ||
        info_header.biHeight != SCOPE_HEIGHT || info_header.biBitCount != 32 ||
        info_header.biCompression != BI_RGB ||
        fseek(file, (long)file_header.bfOffBits, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    for (y = SCOPE_HEIGHT - 1; y >= 0; --y)
        if (fread(browse_pixels + (size_t)y * SCOPE_WIDTH, 4,
                  SCOPE_WIDTH, file) != SCOPE_WIDTH) {
            fclose(file);
            return 0;
        }
    fclose(file);
    return 1;
}

static int load_browse_wave(const wchar_t *path)
{
    FILE *file = _wfopen(path, L"r");
    int ok;
    if (!file) return 0;
    demo_signal_export_wave(&demo, &browse_wave);
    ok = wave_file_read(file, &browse_wave);
    return fclose(file) == 0 && ok;
}

static int open_browse_capture(void)
{
    const char *name = demo_signal_ui_browse_filename(&demo);
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    size_t length;
    if (!name || !capture_directory(directory)) return 0;
    length = strlen(name);
    if (swprintf(path, MAX_PATH, L"%ls\\%hs", directory, name) < 0) return 0;
    if (length >= 4 && _stricmp(name + length - 4, ".bmp") == 0) {
        if (!load_browse_bmp(path)) return 0;
        if (live_snapshot_valid) {
            int selected = demo.browse_selected;
            demo = live_before_browse;
            demo.browse_selected = selected;
            live_snapshot_valid = 0;
        }
        demo_signal_ui_show_capture(&demo, demo.browse_files[demo.browse_selected],
                                    browse_pixels);
    } else {
        if (!load_browse_wave(path)) return 0;
        if (live_snapshot_valid) {
            int selected = demo.browse_selected;
            demo = live_before_browse;
            demo.browse_selected = selected;
            live_snapshot_valid = 0;
        }
        live_before_browse = demo;
        live_snapshot_valid = 1;
        demo_signal_ui_show_wave(&demo, demo.browse_files[demo.browse_selected],
                                 &browse_wave);
    }
    return 1;
}

static int capture(DemoAction action)
{
    wchar_t directory[MAX_PATH];
    wchar_t image_path[MAX_PATH];
    wchar_t wave_path[MAX_PATH];
    wchar_t *separator;
    int index;
    if (!GetModuleFileNameW(NULL, directory, MAX_PATH)) return 0;
    separator = wcsrchr(directory, L'\\');
    if (!separator) return 0;
    *separator = L'\0';
    for (index = 1; index <= 9999; ++index) {
        if (swprintf(image_path, MAX_PATH, L"%ls\\capture_%04d.bmp", directory, index) < 0 ||
            swprintf(wave_path, MAX_PATH, L"%ls\\capture_%04d.csv", directory, index) < 0)
            return 0;
        if (GetFileAttributesW(image_path) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(wave_path) == INVALID_FILE_ATTRIBUTES)
            break;
    }
    if (index > 9999) return 0;
    if (action != DEMO_ACTION_WAVEFORM) {
        render_lcd(screen_pixels);
        if (!save_bmp(image_path, screen_pixels, SCOPE_WIDTH, SCOPE_HEIGHT)) return 0;
    }
    if (action == DEMO_ACTION_WAVEFORM) return save_wave(wave_path);
    if (action == DEMO_ACTION_SCREEN_AND_WAVE && !save_wave(wave_path)) return 0;
    return 1;
}

static void act(HWND window, DemoAction action)
{
    (void)window;
    if (action == DEMO_ACTION_BROWSE_REFRESH) refresh_browse();
    else if (action == DEMO_ACTION_BROWSE_OPEN) {
        if (!open_browse_capture()) demo_signal_notify(&demo, "OPEN FAILED");
    } else if (action == DEMO_ACTION_BROWSE_PREV ||
               action == DEMO_ACTION_BROWSE_NEXT) {
        if ((demo.screen.browser_visible || demo.waveform_loaded) &&
            demo.browse_count) {
            int previous = demo.browse_selected;
            int direction = action == DEMO_ACTION_BROWSE_NEXT ? 1 : -1;
            demo.browse_selected = (previous + direction + demo.browse_count) %
                                   demo.browse_count;
            if (!open_browse_capture()) {
                demo.browse_selected = previous;
                demo_signal_notify(&demo, "OPEN FAILED");
            }
        }
    } else if (action == DEMO_ACTION_BROWSE_DELETE) {
        if (delete_browse_capture()) {
            refresh_browse();
            demo_signal_notify(&demo, "FILE DELETED");
        } else demo_signal_notify(&demo, "DELETE FAILED");
    } else if (action == DEMO_ACTION_BROWSE_CLOSE) {
        if (live_snapshot_valid) {
            int selected = demo.browse_selected;
            demo = live_before_browse;
            demo.browse_selected = selected;
            live_snapshot_valid = 0;
            demo_signal_ui_close_capture(&demo);
        }
    } else if (action != DEMO_ACTION_NONE)
        demo_signal_notify(&demo, capture(action) ?
            action == DEMO_ACTION_SCREENSHOT ? "SCREENSHOT SAVED" :
            action == DEMO_ACTION_WAVEFORM ? "WAVEFORM SAVED" :
            "SCREEN AND WAVE SAVED" :
            "SAVE FAILED");
    invalidate_windows();
}

static void press_control_at(HWND window, DemoControl control, int long_press,
                             ULONGLONG when)
{
    DemoAction action = demo_signal_press_at(&demo, control, long_press, when);
    act(window, action);
}

static void suppress_chord_control(DemoControl control)
{
    int i;
    if (mouse_hold.active && mouse_hold.control == control)
        mouse_hold.suppressed = 1;
    for (i = 0; i < 2; ++i)
        if (key_holds[i].active && key_holds[i].control == control)
            key_holds[i].suppressed = 1;
}

static void begin_press(HWND window, HeldControl *held, DemoControl control,
                        WPARAM key_code, ULONGLONG now)
{
    held->control = control;
    held->key_code = key_code;
    held->since = now;
    held->active = 1;
    held->long_done = 0;
    held->suppressed = 0;
    if (control == DEMO_BTN_CH1 || control == DEMO_BTN_MENU) {
        if (pending_chord != DEMO_CONTROL_COUNT && pending_chord != control &&
            now >= pending_time && now - pending_time <= CHORD_WINDOW_MS) {
            demo = chord_snapshot;
            suppress_chord_control(pending_chord);
            held->suppressed = 1;
            pending_chord = DEMO_CONTROL_COUNT;
            act(window, DEMO_ACTION_SCREEN_AND_WAVE);
            return;
        }
        chord_snapshot = demo;
        pending_chord = control;
        pending_time = now;
    } else {
        pending_chord = DEMO_CONTROL_COUNT;
    }
}

static void finish_press(HWND window, HeldControl *held, ULONGLONG now)
{
    if (!held->active) return;
    if (!held->suppressed && !held->long_done)
        press_control_at(window, held->control,
                         has_hold_action(held->control) &&
                         now - held->since >= (ULONGLONG)hold_time_ms,
                         now);
    held->active = 0;
}

static void fire_long_press(HWND window, HeldControl *held, ULONGLONG now)
{
    if (!held->active || held->suppressed || held->long_done ||
        !has_hold_action(held->control) ||
        now - held->since < (ULONGLONG)hold_time_ms)
        return;
    held->long_done = 1;
    pending_chord = DEMO_CONTROL_COUNT;
    press_control_at(window, held->control, 1, now);
}

static int touch_view_upper(void)
{
    return demo.screen.split_height;
}

static int touch_screen_y(int logical_y)
{
    if (demo.screen.zoom_enabled || demo.screen.fft_enabled) {
        int upper = touch_view_upper();
        return SCOPE_PLOT_Y + upper + logical_y *
               (SCOPE_PLOT_HEIGHT - upper) / SCOPE_PLOT_HEIGHT;
    }
    return SCOPE_PLOT_Y + logical_y;
}

static int touch_plot_y(int screen_y)
{
    if (demo.screen.zoom_enabled || demo.screen.fft_enabled) {
        int upper = touch_view_upper();
        return (screen_y - SCOPE_PLOT_Y - upper) *
               SCOPE_PLOT_HEIGHT / (SCOPE_PLOT_HEIGHT - upper);
    }
    return screen_y - SCOPE_PLOT_Y;
}

static int touch_channel_at(int x, int y, int tolerance)
{
    int sample = x - SCOPE_PLOT_X;
    int distance1, distance2;
    if (sample < 0) sample = 0;
    if (sample >= SCOPE_PLOT_WIDTH) sample = SCOPE_PLOT_WIDTH - 1;
    distance1 = demo.screen.ch1_enabled ?
                abs(y - touch_screen_y(x < SCOPE_PLOT_X + 40 ?
                    demo.screen.channel_zero_y[0] : demo.ch1[sample])) : 10000;
    distance2 = demo.screen.ch2_enabled ?
                abs(y - touch_screen_y(x < SCOPE_PLOT_X + 40 ?
                    demo.screen.channel_zero_y[1] : demo.ch2[sample])) : 10000;
    if (distance1 == 10000 && distance2 == 10000) return -1;
    if (tolerance >= 0 && distance1 > tolerance && distance2 > tolerance) return -1;
    return distance1 <= distance2 ? 0 : 1;
}

static TouchZone touch_zone_at(int x, int y, int *row)
{
    int menu_x = demo.screen.menu_x;
    int menu_y = demo.screen.menu_y;
    int menu_row_y = menu_y + (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y);
    if (x < 0 || x >= SCOPE_WIDTH || y < 0 || y >= SCOPE_HEIGHT)
        return TOUCH_NONE;
    if (demo.screen.browser_visible) {
        if (y < SCOPE_BOTTOM_Y) return TOUCH_NONE;
        if (x >= 12 && x < 152) return TOUCH_BROWSER_PREV;
        if (x >= 160 && x < 300) return TOUCH_BROWSER_NEXT;
        return x >= 872 && x < 1012 ? TOUCH_BROWSER_BACK : TOUCH_NONE;
    }
    if (demo.screen.browser_delete_confirm) {
        int left = SCOPE_BROWSE_CONFIRM_X;
        int top = SCOPE_BROWSE_CONFIRM_Y;
        if (y >= top + 88 && y < top + 132 &&
            x >= left + 216 && x < left + 404)
            return TOUCH_BROWSE_DELETE_CONFIRM;
        return TOUCH_BROWSE_DELETE_CANCEL;
    }
    if (demo.screen.measurement_clear_confirm) {
        int left = SCOPE_MEASURE_CONFIRM_X;
        int top = SCOPE_MEASURE_CONFIRM_Y;
        if (y >= top + 88 && y < top + 132 &&
            x >= left + 216 && x < left + 404)
            return TOUCH_MEASURE_CONFIRM;
        return TOUCH_MEASURE_CANCEL;
    }
    if (demo.screen.measurement_menu && x >= 20 && x < 1004 &&
        y >= 74 && y < SCOPE_BOTTOM_Y - 10) {
        if (y < SCOPE_MEASURE_MENU_ROW_Y)
            return x >= 902 ? TOUCH_MENU_BACK :
                   x >= 588 && x < 728 && y >= 83 && y < 110 ?
                   TOUCH_MEASURE_HIDE :
                   x >= 744 && x < 884 && y >= 83 && y < 110 ?
                   TOUCH_MEASURE_CLEAR : TOUCH_NONE;
        *row = (x >= 512 ? 8 : 0) +
               (y - SCOPE_MEASURE_MENU_ROW_Y) / SCOPE_MEASURE_MENU_ROW_HEIGHT;
        return (y - SCOPE_MEASURE_MENU_ROW_Y) /
               SCOPE_MEASURE_MENU_ROW_HEIGHT < 8 &&
               *row < SCOPE_MEASURE_CATALOG_ITEMS ?
               TOUCH_MENU_ROW : TOUCH_NONE;
    }
    if (demo.screen.menu_open && x >= menu_x &&
        x < menu_x + SCOPE_MENU_WIDTH &&
        !demo.screen.measurement_menu &&
        y >= menu_y && y < menu_row_y +
        demo.screen.menu_count * SCOPE_MENU_ROW_HEIGHT + 6) {
        if (y < menu_row_y)
            return x >= menu_x + SCOPE_MENU_WIDTH - 104 ?
                   TOUCH_MENU_BACK : TOUCH_MENU_DRAG;
        *row = (y - menu_row_y) / SCOPE_MENU_ROW_HEIGHT;
        return *row < demo.screen.menu_count ? TOUCH_MENU_ROW : TOUCH_NONE;
    }
    if (y < SCOPE_PLOT_Y) {
        if (x >= 4 && x < 184) return TOUCH_TOP_MENU;
        if (x >= 188 && x < 368) return TOUCH_TOP_SCREENSHOT;
        if (x >= 372 && x < 548) return TOUCH_TOP_WAVE;
        if (x >= 552 && x < 692) return TOUCH_TOP_FINE;
        if (x >= 696 && x < 852) return TOUCH_TOP_PROCESSING;
        if (x >= 856 && x < 1020) return TOUCH_TOP_RUN;
    }
    if (y >= SCOPE_BOTTOM_Y && y < SCOPE_BOTTOM_Y + SCOPE_BOTTOM_HEIGHT) {
        if (demo.waveform_loaded) {
            if (x >= 12 && x < 152) return TOUCH_BROWSER_PREV;
            if (x >= 160 && x < 300) return TOUCH_BROWSER_NEXT;
            if (x >= 306 && x < 478) return TOUCH_CH1;
            if (x >= 484 && x < 656) return TOUCH_CH2;
            if (x >= 662 && x < 834) return TOUCH_TIME;
            return x >= 840 && x < 1018 ? TOUCH_BOTTOM_CURSOR : TOUCH_NONE;
        }
        if (x >= 6 && x < 200) return TOUCH_CH1;
        if (x >= 206 && x < 400) return TOUCH_CH2;
        if (x >= 406 && x < 600) return TOUCH_TIME;
        if (x >= 606 && x < 800) return TOUCH_TRIGGER_SOURCE;
        if (x >= 806 && x < 1018) return TOUCH_BOTTOM_CURSOR;
    }
    if (demo.screen.menu_open)
        return y >= SCOPE_PLOT_Y && y < SCOPE_BOTTOM_Y ?
               TOUCH_MENU_OUTSIDE : TOUCH_NONE;
    if (x >= SCOPE_PLOT_X && x < SCOPE_PLOT_X + SCOPE_PLOT_WIDTH &&
        y >= SCOPE_PLOT_Y && y < SCOPE_PLOT_Y + SCOPE_PLOT_HEIGHT) {
        int mx, my, mw, mh;
        int cursor_strip_y, cursor_height;
        scope_screen_cursor_measurement_bounds(&demo.screen, NULL, &cursor_strip_y, NULL, &cursor_height);
        if (demo.screen.cursor_measurement_count > 0 &&
            y >= cursor_strip_y && y < cursor_strip_y + cursor_height) {
            if (x >= 0 && x < scope_screen_cursor_measurement_width(&demo.screen))
                return TOUCH_CURSOR_STRIP;
        }
        {
            scope_screen_measurement_bounds(&demo.screen, &mx, &my, &mw, &mh);
            if (x >= mx && x < mx + mw && y >= my && y < my + mh)
                return y < my + 30 && x >= mx + mw -
                       (!demo.screen.measurement_hidden &&
                        demo.screen.measurement_count > 0 &&
                        demo.screen.measurement_horizontal ? 95 : 69) ?
                       TOUCH_MEASURE_LAYOUT : TOUCH_MEASURE_DRAG;
        }
        if ((demo.screen.zoom_enabled || demo.screen.fft_enabled) &&
            abs(y - (SCOPE_PLOT_Y + demo.screen.split_height)) <= 15)
            return TOUCH_SPLIT;
        if (demo.screen.zoom_enabled &&
            y < SCOPE_PLOT_Y + demo.screen.split_height - 26)
            return TOUCH_ZOOM_OVERVIEW;
        if ((demo.screen.zoom_enabled || demo.screen.fft_enabled) &&
            y < SCOPE_PLOT_Y + demo.screen.split_height)
            return demo.screen.fft_cursor_visible ? TOUCH_FFT_CURSOR : TOUCH_NONE;
        if (x >= SCOPE_WIDTH - 36 &&
            (abs(y - touch_screen_y(demo.screen.trigger_y)) <= 23 ||
            (demo.screen.trigger_preview &&
             abs(y - touch_screen_y(demo.screen.trigger_preview_y)) <= 23)))
            return TOUCH_PLOT_TRIGGER;
        if (x < 40 && ((demo.screen.ch1_enabled &&
            abs(y - touch_screen_y(demo.screen.channel_zero_y[0])) < 44) ||
            (demo.screen.ch2_enabled &&
            abs(y - touch_screen_y(demo.screen.channel_zero_y[1])) < 44)))
            return TOUCH_TIME_IN;
        if (demo.screen.cursor_mode != SCOPE_CURSOR_OFF) {
            int coordinate = demo.screen.cursor_mode == SCOPE_CURSOR_TIME ?
                             x - SCOPE_PLOT_X : touch_plot_y(y);
            if (abs(coordinate - demo.screen.cursor_a) <= 64 ||
                abs(coordinate - demo.screen.cursor_b) <= 64)
                return TOUCH_PLOT_CURSOR;
        }
        if (x < 190) return TOUCH_TIME_IN;
        if (x >= 810) return TOUCH_TIME_OUT;
        {
            int channel = touch_channel_at(x, y, -1);
            if (channel >= 0) return channel ? TOUCH_PLOT_CH2 : TOUCH_PLOT_CH1;
        }
    }
    return TOUCH_NONE;
}

static void touch_toggle_menu(DemoMenu kind)
{
    if (demo.screen.menu_open && demo.menu_kind == kind)
        demo_signal_ui_dismiss_menu(&demo);
    else
        demo_signal_ui_open_menu(&demo, kind);
}

static void touch_short(HWND window)
{
    DemoAction action = DEMO_ACTION_NONE;
    switch (touch.zone) {
        case TOUCH_TOP_MENU:
            demo_signal_ui_open_menu(&demo, DEMO_MENU_MAIN); break;
        case TOUCH_BROWSER_BACK:
            demo_signal_ui_close_capture(&demo); break;
        case TOUCH_BROWSER_PREV:
            action = DEMO_ACTION_BROWSE_PREV; break;
        case TOUCH_BROWSER_NEXT:
            action = DEMO_ACTION_BROWSE_NEXT; break;
        case TOUCH_MENU_BACK:
            demo_signal_ui_menu_back(&demo); break;
        case TOUCH_MEASURE_CLEAR:
            demo_signal_ui_menu_select(&demo, SCOPE_MEASURE_CATALOG_ITEMS);
            demo_signal_ui_menu_activate(&demo);
            break;
        case TOUCH_MEASURE_HIDE:
            demo_signal_ui_toggle_measurements_visible(&demo);
            break;
        case TOUCH_MEASURE_CANCEL:
        case TOUCH_MEASURE_CONFIRM:
            demo.screen.measurement_clear_choice =
                touch.zone == TOUCH_MEASURE_CONFIRM;
            demo_signal_ui_menu_activate(&demo);
            break;
        case TOUCH_BROWSE_DELETE_CANCEL:
        case TOUCH_BROWSE_DELETE_CONFIRM:
            demo.screen.browser_delete_choice =
                touch.zone == TOUCH_BROWSE_DELETE_CONFIRM;
            action = demo_signal_ui_menu_activate(&demo);
            break;
        case TOUCH_MENU_OUTSIDE:
            demo_signal_ui_dismiss_menu(&demo); break;
        case TOUCH_TOP_SCREENSHOT:
            action = DEMO_ACTION_SCREENSHOT; break;
        case TOUCH_TOP_WAVE:
            action = DEMO_ACTION_WAVEFORM; break;
        case TOUCH_TOP_FINE:
            demo_signal_ui_toggle_fine(&demo); break;
        case TOUCH_TOP_PROCESSING:
            touch_toggle_menu(DEMO_MENU_PROCESSING); break;
        case TOUCH_BOTTOM_CURSOR:
            touch_toggle_menu(DEMO_MENU_CURSOR); break;
        case TOUCH_TRIGGER_SOURCE:
            touch_toggle_menu(DEMO_MENU_TRIGGER); break;
        case TOUCH_TOP_RUN:
            if (demo.waveform_loaded) action = DEMO_ACTION_BROWSE_CLOSE;
            else demo_signal_ui_toggle_run(&demo);
            break;
        case TOUCH_CH1:
            touch_toggle_menu(DEMO_MENU_CH1); break;
        case TOUCH_CH2:
            touch_toggle_menu(DEMO_MENU_CH2); break;
        case TOUCH_TIME:
            touch_toggle_menu(DEMO_MENU_TIME); break;
        case TOUCH_TIME_IN:
            demo_signal_zoom_time(&demo, -1); break;
        case TOUCH_TIME_OUT:
            demo_signal_zoom_time(&demo, 1); break;
        case TOUCH_MEASURE_LAYOUT:
            if (demo.screen.measurement_hidden)
                demo_signal_ui_toggle_measurements_visible(&demo);
            else if (demo.screen.measurement_count)
                demo_signal_ui_toggle_measurement_layout(&demo);
            else demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
            break;
        case TOUCH_MEASURE_DRAG:
            if (demo.screen.measurement_hidden)
                demo_signal_ui_toggle_measurements_visible(&demo);
            else if (!demo.screen.measurement_count)
                demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
            break;
        case TOUCH_CURSOR_STRIP:
            touch_toggle_menu(DEMO_MENU_CURSOR); break;
        case TOUCH_MENU_ROW:
            if (touch.x >= demo.screen.menu_x +
                           (SCOPE_MENU_CONTROL_X - SCOPE_MENU_X) &&
                touch.x < demo.screen.menu_x +
                          (SCOPE_MENU_CONTROL_X - SCOPE_MENU_X) +
                          SCOPE_MENU_CONTROL_WIDTH &&
                demo.screen.menu_option_count[touch.row]) {
                int count = demo.screen.menu_option_count[touch.row];
                int option = (touch.x - demo.screen.menu_x -
                              (SCOPE_MENU_CONTROL_X - SCOPE_MENU_X)) * count /
                             SCOPE_MENU_CONTROL_WIDTH;
                demo_signal_ui_menu_choose(&demo, touch.row, option);
            } else if (demo.screen.menu_stepper[touch.row]) {
                int control_x = demo.screen.menu_x +
                                (SCOPE_MENU_CONTROL_X - SCOPE_MENU_X);
                if (touch.x >= control_x && touch.x < control_x + 56)
                    action = demo_signal_ui_menu_tap(&demo, touch.row, -1);
                else if (touch.x >= control_x + SCOPE_MENU_CONTROL_WIDTH - 56 &&
                         touch.x < control_x + SCOPE_MENU_CONTROL_WIDTH)
                    action = demo_signal_ui_menu_tap(&demo, touch.row, 1);
                else {
                    demo_signal_ui_menu_select(&demo, touch.row);
                    action = demo_signal_ui_menu_activate(&demo);
                }
            } else action = demo_signal_ui_menu_tap(&demo, touch.row, 1);
            break;
        case TOUCH_PLOT_CURSOR: {
            int target = demo.screen.cursor_mode == SCOPE_CURSOR_TIME ?
                         touch.x - SCOPE_PLOT_X : touch_plot_y(touch.y);
            demo_signal_ui_move_cursor(&demo, target);
            break;
        }
        case TOUCH_ZOOM_OVERVIEW:
            demo_signal_set_zoom_center(&demo, touch.x - SCOPE_PLOT_X);
            break;
        default: break;
    }
    act(window, action);
}

static int fine_touch_delta(int delta, int *remainder)
{
    int total, scaled;
    if (!demo.screen.fine_mode) {
        *remainder = 0;
        return delta;
    }
    total = *remainder + delta;
    scaled = total / 4;
    *remainder = total % 4;
    return scaled;
}

static void pan_time_by_touch(int delta)
{
    delta = fine_touch_delta(delta, &touch.fine_x_remainder);
    if (demo.screen.zoom_enabled && !demo.waveform_loaded) {
        int total = touch.zoom_pan_remainder + delta;
        int source_delta = total / demo.zoom_factor;
        touch.zoom_pan_remainder = total % demo.zoom_factor;
        delta = source_delta * demo.zoom_factor;
    }
    if (delta) demo_signal_ui_pan_time(&demo, delta);
}

static void touch_move(int x, int y)
{
    int movement, threshold, steps;
    int motion_threshold = touch.zone == TOUCH_PLOT_TRIGGER ||
                           touch.zone == TOUCH_SPLIT ||
                           touch.zone == TOUCH_ZOOM_OVERVIEW ||
                           touch.zone == TOUCH_PLOT_CH1 ||
                           touch.zone == TOUCH_PLOT_CH2 ||
                           touch.zone == TOUCH_PLOT_CURSOR ||
                           touch.zone == TOUCH_MEASURE_DRAG ||
                           touch.zone == TOUCH_MEASURE_LAYOUT ? 3 : 12;
    if (touch.zone == TOUCH_MENU_DRAG) motion_threshold = 3;
    if (!touch.active) return;
    if (touch.zone == TOUCH_FFT_CURSOR) {
        if (x != touch.last_x || y != touch.last_y) {
            int vertical = demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE;
            int coordinate = vertical ? scope_screen_fft_level_at(&demo.screen, y) : x - SCOPE_PLOT_X;
            int previous = vertical ? scope_screen_fft_level_at(&demo.screen, touch.last_y) :
                                      touch.last_x - SCOPE_PLOT_X;
            demo_signal_ui_drag_fft_cursor(&demo, coordinate, previous,
                vertical ? &touch.fine_y_remainder : &touch.fine_x_remainder);
            touch.last_x = x;
            touch.last_y = y;
            touch.moved = 1;
            invalidate_windows();
        }
        return;
    }
    if (abs(x - touch.x) > motion_threshold ||
        abs(y - touch.y) > motion_threshold) touch.moved = 1;
    if (!touch.moved) return;
    movement = x - touch.last_x;
    threshold = 30;
    switch (touch.zone) {
        case TOUCH_MENU_DRAG:
            demo_signal_ui_move_menu(&demo, x - touch.last_x, y - touch.last_y);
            touch.last_x = x;
            touch.last_y = y;
            invalidate_windows();
            return;
        case TOUCH_CH1: case TOUCH_CH2: case TOUCH_TIME:
            if (abs(x - touch.x) <= abs(y - touch.y) + 4) return;
            touch.remainder += movement;
            steps = touch.remainder / 36;
            touch.remainder %= 36;
            touch.last_x = x;
            touch.last_y = y;
            if (steps) {
                if (touch.zone == TOUCH_TIME)
                    demo_signal_zoom_time(&demo, steps);
                else
                    demo_signal_zoom_channel(&demo, touch.zone == TOUCH_CH2, -steps);
                invalidate_windows();
            }
            return;
        case TOUCH_MENU_ROW: {
            int row;
            if (demo.screen.measurement_menu) {
                int local_row = (y - SCOPE_MEASURE_MENU_ROW_Y) /
                                SCOPE_MEASURE_MENU_ROW_HEIGHT;
                if (local_row < 0) local_row = 0;
                if (local_row > 7) local_row = 7;
                row = (x >= 512 ? 8 : 0) + local_row;
                if (row >= demo.screen.menu_count) row = demo.screen.menu_count - 1;
            } else {
                int row_y = demo.screen.menu_y +
                            (SCOPE_MENU_ROW_Y - SCOPE_MENU_Y);
                row = (y - row_y) / SCOPE_MENU_ROW_HEIGHT;
                if (y < row_y) row = 0;
                if (row >= demo.screen.menu_count) row = demo.screen.menu_count - 1;
            }
            demo_signal_ui_menu_select(&demo, row);
            touch.last_x = x;
            touch.last_y = y;
            invalidate_windows();
            return;
        }
        case TOUCH_PLOT_TRIGGER:
            if (demo.screen.fine_mode) {
                int delta = fine_touch_delta(touch_plot_y(y) - touch_plot_y(touch.last_y),
                                             &touch.fine_y_remainder);
                if (delta)
                    demo_signal_set_trigger_preview_y(&demo,
                        (demo.screen.trigger_preview ? demo.screen.trigger_preview_y :
                         demo.screen.trigger_y) + delta);
            } else demo_signal_set_trigger_preview_y(&demo, touch_plot_y(y));
            touch.last_y = y;
            invalidate_windows();
            return;
        case TOUCH_SPLIT:
            if (demo.screen.fine_mode) {
                int delta = fine_touch_delta(y - touch.last_y,
                                             &touch.fine_y_remainder);
                if (delta)
                    demo_signal_ui_set_split_height(&demo,
                                                    demo.screen.split_height + delta);
            } else demo_signal_ui_set_split_height(&demo, y - SCOPE_PLOT_Y);
            touch.last_y = y;
            invalidate_windows();
            return;
        case TOUCH_PLOT_CH1: case TOUCH_PLOT_CH2:
            if (!touch.axis) {
                int dx = abs(x - touch.x), dy = abs(y - touch.y);
                if (dx + dy < 7) return;
                touch.axis = dy >= dx ? 1 : 2;
            }
            if (touch.axis == 1) {
                int delta = fine_touch_delta(touch_plot_y(y) -
                                             touch_plot_y(touch.last_y),
                                             &touch.fine_y_remainder);
                if (delta)
                    demo_signal_move_channel(&demo, touch.zone == TOUCH_PLOT_CH2, delta);
            } else pan_time_by_touch(x - touch.last_x);
            touch.last_x = x;
            touch.last_y = y;
            invalidate_windows();
            return;
        case TOUCH_ZOOM_OVERVIEW:
            if (demo.screen.fine_mode) {
                int delta = fine_touch_delta(x - touch.last_x,
                                             &touch.fine_x_remainder);
                if (delta)
                    demo_signal_set_zoom_center(&demo,
                        (demo.screen.zoom_window_start +
                         demo.screen.zoom_window_end) / 2 + delta);
            } else demo_signal_set_zoom_center(&demo, x - SCOPE_PLOT_X);
            touch.last_x = x;
            invalidate_windows();
            return;
        case TOUCH_PLOT_CURSOR:
            if (demo.screen.fine_mode) {
                int delta = demo.screen.cursor_mode == SCOPE_CURSOR_TIME ?
                    fine_touch_delta(x - touch.last_x, &touch.fine_x_remainder) :
                    fine_touch_delta(touch_plot_y(y) - touch_plot_y(touch.last_y),
                                     &touch.fine_y_remainder);
                if (delta)
                    demo_signal_ui_move_cursor(&demo,
                        (demo.screen.cursor_selected ? demo.screen.cursor_b :
                         demo.screen.cursor_a) + delta);
            } else demo_signal_ui_move_cursor(&demo,
                demo.screen.cursor_mode == SCOPE_CURSOR_TIME ?
                x - SCOPE_PLOT_X : touch_plot_y(y));
            touch.last_x = x;
            touch.last_y = y;
            invalidate_windows();
            return;
        case TOUCH_TIME_IN: case TOUCH_TIME_OUT:
            if (abs(y - touch.y) > abs(x - touch.x) + 4) {
                int channel = touch_channel_at(touch.x, touch.y, -1);
                if (channel >= 0) {
                    touch.zone = channel ? TOUCH_PLOT_CH2 : TOUCH_PLOT_CH1;
                    touch.axis = 1;
                    int delta = fine_touch_delta(touch_plot_y(y) -
                                                 touch_plot_y(touch.last_y),
                                                 &touch.fine_y_remainder);
                    if (delta) demo_signal_move_channel(&demo, channel, delta);
                    touch.last_x = x;
                    touch.last_y = y;
                    invalidate_windows();
                }
            }
            return;
        case TOUCH_MEASURE_DRAG: case TOUCH_MEASURE_LAYOUT:
            demo_signal_ui_move_measurements(&demo,
                fine_touch_delta(x - touch.last_x, &touch.fine_x_remainder),
                fine_touch_delta(y - touch.last_y, &touch.fine_y_remainder));
            touch.last_x = x;
            touch.last_y = y;
            invalidate_windows();
            return;
        default: return;
    }
    touch.remainder += movement;
    steps = touch.remainder / threshold;
    touch.remainder %= threshold;
    touch.last_x = x;
    touch.last_y = y;
    if (steps) {
        demo_signal_ui_menu_adjust(&demo, steps);
        invalidate_windows();
    }
}

static void fire_lcd_touch_hold(ULONGLONG now)
{
    if (!touch.active || touch.moved || touch.long_done ||
        now - touch.since < (ULONGLONG)hold_time_ms) return;
    switch (touch.zone) {
        case TOUCH_TOP_MENU: demo_signal_ui_toggle_zoom(&demo); break;
        case TOUCH_CH1: demo_signal_ui_reset_channel_position(&demo, 0); break;
        case TOUCH_CH2: demo_signal_ui_reset_channel_position(&demo, 1); break;
        case TOUCH_TIME: demo_signal_ui_reset_time_position(&demo); break;
        case TOUCH_TRIGGER_SOURCE:
            demo_signal_ui_cycle_trigger_mode(&demo); break;
        case TOUCH_BOTTOM_CURSOR:
            demo_signal_ui_cycle_cursor_mode(&demo); break;
        case TOUCH_MEASURE_DRAG: case TOUCH_MEASURE_LAYOUT:
            demo_signal_ui_open_menu(&demo, DEMO_MENU_MEASURE);
            touch.zone = TOUCH_NONE;
            break;
        case TOUCH_MENU_ROW:
            if (demo.menu_kind != DEMO_MENU_BROWSE || !demo.browse_count) return;
            demo_signal_ui_request_browse_delete(&demo, touch.row);
            touch.zone = TOUCH_NONE;
            break;
        default: return;
    }
    touch.long_done = 1;
    invalidate_windows();
}

static int create_buffers(void)
{
    BITMAPINFO screen_info = {0};
    BITMAPINFO panel_info = {0};
    screen_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    screen_info.bmiHeader.biWidth = SCOPE_WIDTH;
    screen_info.bmiHeader.biHeight = -SCOPE_HEIGHT;
    screen_info.bmiHeader.biPlanes = 1;
    screen_info.bmiHeader.biBitCount = 32;
    screen_info.bmiHeader.biCompression = BI_RGB;
    panel_info.bmiHeader = screen_info.bmiHeader;
    panel_info.bmiHeader.biWidth = PANEL_WIDTH;
    panel_info.bmiHeader.biHeight = -PANEL_HEIGHT;
    screen_dc = CreateCompatibleDC(NULL);
    panel_dc = CreateCompatibleDC(NULL);
    if (!screen_dc || !panel_dc) return 0;
    screen_bitmap = CreateDIBSection(screen_dc, &screen_info, DIB_RGB_COLORS,
                                     (void **)&screen_pixels, NULL, 0);
    panel_bitmap = CreateDIBSection(panel_dc, &panel_info, DIB_RGB_COLORS,
                                    (void **)&panel_pixels, NULL, 0);
    if (!screen_bitmap || !panel_bitmap || !screen_pixels || !panel_pixels) return 0;
    old_screen_bitmap = SelectObject(screen_dc, screen_bitmap);
    old_panel_bitmap = SelectObject(panel_dc, panel_bitmap);
    return 1;
}

static void release_buffers(void)
{
    if (old_screen_bitmap) SelectObject(screen_dc, old_screen_bitmap);
    if (old_panel_bitmap) SelectObject(panel_dc, old_panel_bitmap);
    if (screen_bitmap) DeleteObject(screen_bitmap);
    if (panel_bitmap) DeleteObject(panel_bitmap);
    if (screen_dc) DeleteDC(screen_dc);
    if (panel_dc) DeleteDC(panel_dc);
}

static LRESULT CALLBACK controls_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_CREATE:
            SetTimer(window, 1, 25, NULL);
            return 0;
        case WM_TIMER: {
            int refresh = demo.screen.running || demo.notice_ticks > 0;
            int i;
            ULONGLONG now = GetTickCount64();
            if (splash_until) {
                refresh = 1;
                if (now >= splash_until) splash_until = 0;
            }
            if (pending_chord != DEMO_CONTROL_COUNT &&
                now - pending_time > CHORD_WINDOW_MS)
                pending_chord = DEMO_CONTROL_COUNT;
            fire_long_press(window, &mouse_hold, now);
            for (i = 0; i < 2; ++i)
                fire_long_press(window, &key_holds[i], now);
            fire_lcd_touch_hold(now);
            if (mouse_hold.active || key_holds[0].active || key_holds[1].active)
                refresh = 1;
            demo_signal_advance(&demo);
            if (refresh) invalidate_windows();
            return 0;
        }
        case WM_MOUSEMOVE: {
            DemoControl next = panel_hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (next != hovered) {
                hovered = next;
                invalidate_windows();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int adjustment = panel_hold_adjustment(GET_X_LPARAM(lparam),
                                                   GET_Y_LPARAM(lparam));
            if (adjustment) {
                hold_time_ms += adjustment * 100;
                if (hold_time_ms < 200) hold_time_ms = 200;
                if (hold_time_ms > 1500) hold_time_ms = 1500;
                invalidate_windows();
                return 0;
            }
            DemoControl control = panel_hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (control != DEMO_CONTROL_COUNT && !mouse_hold.active) {
                begin_press(window, &mouse_hold, control, 0, GetTickCount64());
                SetCapture(window);
                invalidate_windows();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            finish_press(window, &mouse_hold, GetTickCount64());
            if (GetCapture() == window) ReleaseCapture();
            invalidate_windows();
            return 0;
        }
        case WM_CAPTURECHANGED:
            mouse_hold.active = 0;
            invalidate_windows();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            DemoControl control;
            ScreenToClient(window, &point);
            control = panel_hit_test(point.x, point.y);
            if (panel_is_encoder(control)) {
                int steps;
                pending_chord = DEMO_CONTROL_COUNT;
                wheel_remainder += (short)HIWORD(wparam);
                steps = wheel_remainder / WHEEL_DELTA;
                wheel_remainder %= WHEEL_DELTA;
                demo_signal_rotate(&demo, control, steps);
                invalidate_windows();
            }
            return 0;
        }
        case WM_KEYDOWN: {
            DemoControl control = DEMO_CONTROL_COUNT;
            ULONGLONG now = GetTickCount64();
            int i;
            if ((lparam & 0x40000000) && wparam != VK_UP && wparam != VK_DOWN)
                return 0;
            switch (wparam) {
                case '1': control = DEMO_BTN_CH1; break;
                case '2': control = DEMO_BTN_CH2; break;
                case VK_SPACE: control = DEMO_BTN_RUN; break;
                case VK_UP: pending_chord = DEMO_CONTROL_COUNT;
                            demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, 1); break;
                case VK_DOWN: pending_chord = DEMO_CONTROL_COUNT;
                              demo_signal_rotate(&demo, DEMO_ENC_TRIGGER, -1); break;
                case VK_RETURN: control = DEMO_ENC_TRIGGER; break;
                case 'C': control = DEMO_BTN_CURSOR; break;
                case 'T': control = DEMO_BTN_MODE; break;
                case 'M': control = DEMO_BTN_MENU; break;
                case VK_F2:
                case VK_F3: pending_chord = DEMO_CONTROL_COUNT; return 0;
                default: return DefWindowProcW(window, message, wparam, lparam);
            }
            if (control != DEMO_CONTROL_COUNT) {
                for (i = 0; i < 2; ++i)
                    if (key_holds[i].active && key_holds[i].key_code == wparam)
                        return 0;
                for (i = 0; i < 2; ++i)
                    if (!key_holds[i].active) {
                        begin_press(window, &key_holds[i], control, wparam, now);
                        break;
                    }
            }
            invalidate_windows();
            return 0;
        }
        case WM_KEYUP: {
            int i;
            if (wparam == VK_F2 || wparam == VK_F3)
                act(window, wparam == VK_F2 ?
                    DEMO_ACTION_SCREENSHOT : DEMO_ACTION_SCREEN_AND_WAVE);
            for (i = 0; i < 2; ++i)
                if (key_holds[i].active && key_holds[i].key_code == wparam)
                    finish_press(window, &key_holds[i], GetTickCount64());
            invalidate_windows();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            draw_all();
            BitBlt(dc, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, panel_dc, 0, 0, SRCCOPY);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            controls_window = NULL;
            if (lcd_window) {
                HWND other = lcd_window;
                lcd_window = NULL;
                DestroyWindow(other);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void lcd_touch_begin(int x, int y)
{
    if (touch.active) return;
    touch.zone = touch_zone_at(x, y, &touch.row);
    if (touch.zone == TOUCH_NONE) return;
    touch.x = touch.last_x = x;
    touch.y = touch.last_y = y;
    touch.remainder = 0;
    touch.fine_x_remainder = 0;
    touch.fine_y_remainder = 0;
    touch.zoom_pan_remainder = 0;
    touch.axis = 0;
    touch.moved = 0;
    touch.since = GetTickCount64();
    touch.long_done = 0;
    touch.active = 1;
    if (touch.zone == TOUCH_FFT_CURSOR) {
        if (demo.screen.fine_mode) demo_signal_ui_select_cursor(&demo, SCOPE_CURSOR_SELECT_FFT);
        else demo_signal_ui_move_fft_cursor(&demo, demo.screen.cursor_mode == SCOPE_CURSOR_VOLTAGE ?
                 scope_screen_fft_level_at(&demo.screen, y) : x - SCOPE_PLOT_X);
        invalidate_windows();
    }
    if (touch.zone == TOUCH_PLOT_CURSOR) {
        int coord = demo.screen.cursor_mode == SCOPE_CURSOR_TIME ?
                    x - SCOPE_PLOT_X : touch_plot_y(y);
        int first = demo.screen.cursor_a;
        int second = demo.screen.cursor_b;
        demo_signal_ui_select_cursor(&demo, abs(coord - second) < abs(coord - first));
    }
}

static void lcd_touch_end(HWND window, int y)
{
    if (!touch.active) return;
    fire_lcd_touch_hold(GetTickCount64());
    if (touch.zone == TOUCH_PLOT_TRIGGER) {
        if (!demo.screen.fine_mode)
            demo_signal_set_trigger_preview_y(&demo, touch_plot_y(y));
        demo_signal_ui_apply_trigger(&demo);
    } else if (!touch.moved && !touch.long_done) {
        touch_short(window);
    }
    touch.active = 0;
    invalidate_windows();
}

static int contact_count(void)
{
    return contacts[0].active + contacts[1].active;
}

static unsigned long long contact_distance_sq(void)
{
    long long dx = contacts[0].point.x - contacts[1].point.x;
    long long dy = contacts[0].point.y - contacts[1].point.y;
    return (unsigned long long)(dx * dx + dy * dy);
}

static int handle_touch_pointer(HWND window, UINT message, WPARAM wparam)
{
    UINT32 id = GET_POINTERID_WPARAM(wparam);
    POINTER_INPUT_TYPE type;
    POINTER_INFO info;
    POINT point;
    int i, slot = -1;
    if (!GetPointerType(id, &type) || type != PT_TOUCH ||
        !GetPointerInfo(id, &info)) return 0;
    point = info.ptPixelLocation;
    ScreenToClient(window, &point);
    for (i = 0; i < 2; ++i)
        if (contacts[i].active && contacts[i].id == id) slot = i;
    if (message == WM_POINTERDOWN) {
        if (slot >= 0) {
            contacts[slot].active = 0;
            touch.active = 0;
            pinch_block = 0;
            slot = -1;
        }
        if (slot < 0)
            for (i = 0; i < 2; ++i)
                if (!contacts[i].active) { slot = i; break; }
        if (slot < 0) return 1;
        contacts[slot].id = id;
        contacts[slot].point = point;
        contacts[slot].active = 1;
        if (contact_count() == 1 && !pinch_block) lcd_touch_begin(point.x, point.y);
        else if (contact_count() == 2) {
            pinch_block = 1;
            pinch_consumed = 0;
            pinch_start_sq = contact_distance_sq();
            touch.active = 0;
        }
    } else if (slot >= 0 && message == WM_POINTERUPDATE) {
        contacts[slot].point = point;
        if (contact_count() == 2 && !pinch_consumed && pinch_start_sq > 100) {
            unsigned long long distance = contact_distance_sq();
            if (!demo.screen.zoom_enabled && distance * 100 > pinch_start_sq * 125) {
                demo_signal_ui_toggle_zoom(&demo);
                pinch_consumed = 1;
                invalidate_windows();
            } else if (demo.screen.zoom_enabled && distance * 100 < pinch_start_sq * 80) {
                demo_signal_ui_toggle_zoom(&demo);
                pinch_consumed = 1;
                invalidate_windows();
            }
        } else if (!pinch_block && contact_count() == 1) {
            touch_move(point.x, point.y);
        }
    } else if (slot >= 0 && message == WM_POINTERUP) {
        if (!pinch_block && contact_count() == 1)
            lcd_touch_end(window, point.y);
        contacts[slot].active = 0;
        if (!contact_count()) {
            pinch_block = 0;
            pinch_start_sq = 0;
        }
    }
    return 1;
}

static int mouse_from_touch(void)
{
    ULONG_PTR extra = (ULONG_PTR)GetMessageExtraInfo();
    return (extra & 0xffffff00u) == 0xff515700u;
}

static LRESULT CALLBACK lcd_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            if (handle_touch_pointer(window, message, wparam)) return 0;
            break;
        case WM_POINTERCAPTURECHANGED:
            touch.active = 0;
            contacts[0].active = contacts[1].active = 0;
            pinch_block = 0;
            pinch_start_sq = 0;
            return 0;
        case WM_LBUTTONDOWN:
            if (mouse_from_touch()) return 0;
            lcd_touch_begin(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (touch.active) SetCapture(window);
            return 0;
        case WM_MOUSEMOVE:
            if (mouse_from_touch()) return 0;
            touch_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_LBUTTONUP:
            if (mouse_from_touch()) return 0;
            lcd_touch_end(window, GET_Y_LPARAM(lparam));
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            touch.active = 0;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            render_lcd(screen_pixels);
            BitBlt(dc, 0, 0, SCOPE_WIDTH, SCOPE_HEIGHT, screen_dc, 0, 0, SRCCOPY);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            lcd_window = NULL;
            if (controls_window) DestroyWindow(controls_window);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    WNDCLASSW controls_class = {0};
    WNDCLASSW lcd_class = {0};
    RECT window_rect = {0, 0, PANEL_WIDTH, PANEL_HEIGHT};
    DisplayLayout layout = {0};
    MSG message;
    int controls_x, controls_y;
    int argument_count = 0;
    LPWSTR *arguments;
    int snapshot_result = -1;
    int windowed = 0, argument;
    const wchar_t *snapshot_path = NULL;
    int panel_snapshot = 0;
    (void)previous;
    (void)command_line;

    SetProcessDPIAware();
    demo_signal_init(&demo);
    if (!create_buffers() || !panel_init()) {
        MessageBoxW(NULL, L"Cannot create preview buffers.", L"Oscill LCD", MB_ICONERROR);
        release_buffers();
        panel_cleanup();
        return 1;
    }
    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    for (argument = 1; arguments && argument < argument_count; ++argument) {
        const wchar_t *option = arguments[argument];
        if (lstrcmpW(option, L"--windowed") == 0) windowed = 1;
        else if (lstrcmpW(option, L"--splash") == 0)
            splash_until = GetTickCount64() + 3000;
        else if (lstrcmpW(option, L"--cursors") == 0)
            demo_signal_ui_cycle_cursor_mode(&demo);
        else if (lstrcmpW(option, L"--fft") == 0) {
            demo_signal_ui_open_menu(&demo, DEMO_MENU_PROCESSING);
            demo_signal_ui_menu_choose(&demo, 4, 1);
            demo_signal_ui_dismiss_menu(&demo);
        } else if (lstrcmpW(option, L"--menu") == 0 && argument + 1 < argument_count) {
            const wchar_t *name = arguments[++argument];
            DemoMenu menu = lstrcmpW(name, L"main") == 0 ? DEMO_MENU_MAIN :
                            lstrcmpW(name, L"debug") == 0 ? DEMO_MENU_DEBUG :
                            lstrcmpW(name, L"measurements") == 0 ? DEMO_MENU_MEASURE :
                            lstrcmpW(name, L"processing") == 0 ? DEMO_MENU_PROCESSING :
                            lstrcmpW(name, L"ch1") == 0 ? DEMO_MENU_CH1 : DEMO_MENU_NONE;
            if (menu == DEMO_MENU_NONE) { snapshot_result = 2; break; }
            demo_signal_ui_open_menu(&demo, menu);
        } else if ((lstrcmpW(option, L"--snapshot") == 0 ||
                    lstrcmpW(option, L"--panel-snapshot") == 0) && argument + 1 < argument_count) {
            panel_snapshot = lstrcmpW(option, L"--panel-snapshot") == 0;
            snapshot_path = arguments[++argument];
        } else { snapshot_result = 2; break; }
    }
    if (snapshot_result < 0 && snapshot_path) {
        draw_all();
        snapshot_result = panel_snapshot ?
            (save_bmp(snapshot_path, panel_pixels, PANEL_WIDTH, PANEL_HEIGHT) ? 0 : 1) :
            (save_bmp(snapshot_path, screen_pixels, SCOPE_WIDTH, SCOPE_HEIGHT) ? 0 : 1);
    }
    if (arguments) LocalFree(arguments);
    if (snapshot_result >= 0) {
        release_buffers();
        panel_cleanup();
        return snapshot_result;
    }
    EnumDisplayMonitors(NULL, NULL, find_display, (LPARAM)&layout);
    if (!layout.primary_found || (!layout.lcd_found && !windowed)) {
        MessageBoxW(NULL, L"A separate 1024x600 display was not found. Set the LCD to Extend mode in Windows.",
                    L"Oscill LCD", MB_ICONERROR);
        release_buffers();
        panel_cleanup();
        return 1;
    }
    controls_class.lpfnWndProc = controls_window_proc;
    controls_class.hInstance = instance;
    controls_class.lpszClassName = L"OscillLCDControls";
    controls_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    controls_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    lcd_class = controls_class;
    lcd_class.lpfnWndProc = lcd_window_proc;
    lcd_class.lpszClassName = L"OscillLCDScreen";
    if (!RegisterClassW(&controls_class) || !RegisterClassW(&lcd_class)) {
        release_buffers();
        panel_cleanup();
        return 1;
    }
    AdjustWindowRectEx(&window_rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE, 0);
    controls_x = layout.primary_work.left +
                 (layout.primary_work.right - layout.primary_work.left -
                  (window_rect.right - window_rect.left)) / 2;
    controls_y = layout.primary_work.top +
                 (layout.primary_work.bottom - layout.primary_work.top -
                  (window_rect.bottom - window_rect.top)) / 2;
    controls_window = CreateWindowExW(0, controls_class.lpszClassName, L"Oscill controls",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                     controls_x, controls_y,
                                     window_rect.right - window_rect.left,
                                     window_rect.bottom - window_rect.top,
                                     NULL, NULL, instance, NULL);
    if (!controls_window) {
        release_buffers();
        panel_cleanup();
        return 1;
    }
    if (windowed) {
        RECT lcd_rect = {0, 0, SCOPE_WIDTH, SCOPE_HEIGHT};
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectEx(&lcd_rect, style, FALSE, 0);
        lcd_window = CreateWindowExW(0, lcd_class.lpszClassName, L"Oscill LCD preview",
                                    style, layout.primary_work.left + 20,
                                    layout.primary_work.top + 20,
                                    lcd_rect.right - lcd_rect.left, lcd_rect.bottom - lcd_rect.top,
                                    NULL, NULL, instance, NULL);
    } else {
        lcd_window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                     lcd_class.lpszClassName, L"Oscill LCD",
                                     WS_POPUP, layout.lcd_bounds.left, layout.lcd_bounds.top,
                                     SCOPE_WIDTH, SCOPE_HEIGHT,
                                     NULL, NULL, instance, NULL);
    }
    if (!lcd_window) {
        DestroyWindow(controls_window);
        release_buffers();
        panel_cleanup();
        return 1;
    }
    ShowWindow(lcd_window, SW_SHOWNOACTIVATE);
    ShowWindow(controls_window, show);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    release_buffers();
    panel_cleanup();
    return (int)message.wParam;
}
