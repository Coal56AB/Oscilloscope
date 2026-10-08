#include "panel_win32.h"

#include <math.h>
#include <wchar.h>

#define COL(r,g,b) RGB((r),(g),(b))

typedef struct {
    DemoControl id;
    int x;
    int y;
    int radius;
    const wchar_t *label;
    const wchar_t *hint;
    COLORREF accent;
    int encoder;
} ControlVisual;

static const ControlVisual controls[] = {
    {DEMO_ENC_CH1, 409, 754, 48, L"CH1", L"V/DIV / POSITION", COL(255,196,77), 1},
    {DEMO_ENC_CH2, 554, 754, 48, L"CH2", L"V/DIV / POSITION", COL(82,210,235), 1},
    {DEMO_ENC_TIME, 704, 754, 48, L"TIME", L"TIME / POSITION", COL(195,211,219), 1},
    {DEMO_ENC_TRIGGER, 849, 754, 48, L"TRIGGER", L"PREVIEW / APPLY", COL(112,223,163), 1},
    {DEMO_ENC_FUNCTION, 994, 754, 48, L"FUNCTION", L"HOLD: FINE", COL(195,211,219), 1},
    {DEMO_BTN_CH1, 409, 865, 23, L"EN CH1", L"ON / MENU / 2X OFF", COL(255,196,77), 0},
    {DEMO_BTN_CH2, 554, 865, 23, L"EN CH2", L"ON / MENU / 2X OFF", COL(82,210,235), 0},
    {DEMO_BTN_CURSOR, 704, 865, 23, L"CURSOR", L"HOLD: SETTINGS", COL(195,172,255), 0},
    {DEMO_BTN_MODE, 849, 865, 23, L"TRG MODE", L"HOLD: SETTINGS", COL(112,223,163), 0},
    {DEMO_BTN_MENU, 994, 865, 23, L"MENU / BACK", L"HOLD: ZOOM", COL(195,211,219), 0},
    {DEMO_BTN_RUN, 1159, 841, 23, L"RUN / STOP", L"HOLD: FORCE TRIG", COL(112,223,163), 0}
};

static HFONT heading_font;
static HFONT label_font;
static HFONT hint_font;

int panel_init(void)
{
    heading_font = CreateFontW(-19, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    label_font = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    hint_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    return heading_font && label_font && hint_font;
}

void panel_cleanup(void)
{
    if (heading_font) DeleteObject(heading_font);
    if (label_font) DeleteObject(label_font);
    if (hint_font) DeleteObject(hint_font);
}

static void fill_rect(HDC dc, int x, int y, int w, int h, COLORREF color)
{
    RECT rect = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void outline(HDC dc, int x, int y, int w, int h, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void ellipse(HDC dc, int x, int y, int radius, COLORREF fill, COLORREF edge, int edge_width)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, edge_width, edge);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void centered(HDC dc, int x, int y, int width, int height,
                     const wchar_t *value, HFONT font, COLORREF color)
{
    RECT rect = {x - width / 2, y, x + width / 2, y + height};
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static void centered_ascii(HDC dc, int x, int y, int width, int height,
                           const char *value, HFONT font, COLORREF color)
{
    wchar_t wide[64];
    if (!value) return;
    MultiByteToWideChar(CP_UTF8, 0, value, -1, wide, 64);
    centered(dc, x, y, width, height, wide, font, color);
}

static void section(HDC dc, int x, int width, const wchar_t *title)
{
    fill_rect(dc, x, 647, width, 286, COL(22,38,47));
    outline(dc, x, 647, width, 286, COL(64,87,96));
    fill_rect(dc, x + 1, 648, width - 2, 35, COL(43,63,72));
    centered(dc, x + width / 2, 650, width - 8, 28, title, heading_font, COL(227,238,241));
}

static void jack(HDC dc, int x, const wchar_t *name, COLORREF accent)
{
    centered(dc, x, 687, 130, 21, name, label_font, COL(222,235,238));
    ellipse(dc, x, 761, 49, COL(192,202,202), accent, 6);
    ellipse(dc, x, 761, 36, COL(61,76,78), COL(124,143,145), 3);
    ellipse(dc, x, 761, 23, COL(207,215,215), COL(33,45,47), 3);
    ellipse(dc, x, 761, 9, COL(24,35,39), COL(22,31,34), 1);
    centered(dc, x, 836, 130, 22, L"BNC INPUT", hint_font, COL(149,171,180));
}

static int control_active(const DemoSignal *demo, DemoControl id)
{
    switch (id) {
        case DEMO_BTN_CH1: return demo->screen.ch1_enabled;
        case DEMO_BTN_CH2: return demo->screen.ch2_enabled;
        case DEMO_BTN_CURSOR: return demo->screen.cursor_mode != SCOPE_CURSOR_OFF;
        case DEMO_BTN_MODE: return demo->trigger_mode_index != 0;
        case DEMO_BTN_MENU: return demo->screen.menu_open || demo->screen.zoom_enabled;
        case DEMO_BTN_RUN: return demo->screen.running;
        default: return 0;
    }
}

static int control_angle(const DemoSignal *demo, DemoControl id)
{
    if (id == DEMO_ENC_CH1) return demo->scale_index[0] * 25 + demo->channel_position[0] / 4;
    if (id == DEMO_ENC_CH2) return demo->scale_index[1] * 25 + demo->channel_position[1] / 4;
    if (id == DEMO_ENC_TIME) return demo->time_index * 20 + demo->time_position / 32;
    if (id == DEMO_ENC_TRIGGER) return demo->screen.trigger_preview_y / 4;
    return demo->screen.menu_selected * 22 + demo->screen.cursor_a / 20;
}

static void draw_knob(HDC dc, const ControlVisual *control, const DemoSignal *demo,
                      DemoControl pressed, DemoControl hovered)
{
    double angle = (-100.0 + control_angle(demo, control->id)) * 3.14159265358979323846 / 180.0;
    int x = control->x;
    int y = control->y;
    HPEN mark = CreatePen(PS_SOLID, 4, COL(231,242,242));
    HGDIOBJ old_pen;
    ellipse(dc, x, y, 48, COL(8,15,19), control->accent,
            hovered == control->id ? 6 : 4);
    ellipse(dc, x, y, 39, pressed == control->id ? COL(58,75,81) : COL(27,39,44),
            COL(15,25,30), 2);
    ellipse(dc, x - 8, y - 9, 14, COL(37,51,55), COL(37,51,55), 1);
    old_pen = SelectObject(dc, mark);
    MoveToEx(dc, x + (int)(24 * cos(angle)), y + (int)(24 * sin(angle)), NULL);
    LineTo(dc, x + (int)(35 * cos(angle)), y + (int)(35 * sin(angle)));
    SelectObject(dc, old_pen);
    DeleteObject(mark);
    centered(dc, x, 684, 145, 20, control->label, label_font, COL(226,238,240));
    centered(dc, x, 804, 145, 16,
             control->id == DEMO_ENC_FUNCTION && demo->screen.menu_open ?
             L"HOLD: BACK" : control->hint,
             hint_font, COL(149,171,180));
}

static void draw_button(HDC dc, const ControlVisual *control, const DemoSignal *demo,
                        DemoControl pressed, DemoControl hovered)
{
    int x = control->x;
    int y = control->y;
    int radius = control->radius;
    COLORREF border = control_active(demo, control->id) ? control->accent : COL(85,105,113);
    fill_rect(dc, x - radius, y - radius, radius * 2, radius * 2, border);
    fill_rect(dc, x - radius + 4, y - radius + 4, radius * 2 - 8, radius * 2 - 8,
              pressed == control->id ? COL(67,86,92) :
              hovered == control->id ? COL(42,58,64) : COL(24,34,39));
    centered(dc, x, control->id == DEMO_BTN_RUN ? 788 : 821,
             control->id == DEMO_BTN_RUN ? 160 : 145, 19,
             control->id == DEMO_BTN_RUN && demo->waveform_loaded ?
             L"CSV" : control->label, label_font, COL(228,239,241));
    centered(dc, x, control->id == DEMO_BTN_RUN ? 880 : 891,
             control->id == DEMO_BTN_RUN ? 170 : 145,
             control->id == DEMO_BTN_RUN ? 22 : 20,
             control->id == DEMO_BTN_RUN && demo->waveform_loaded ?
             L"RETURN TO CAPTURE" : control->hint, hint_font, COL(149,171,180));
}

int panel_is_encoder(DemoControl control)
{
    return control >= DEMO_ENC_CH1 && control <= DEMO_ENC_FUNCTION;
}

int panel_hold_adjustment(int x, int y)
{
    if (y < 306 || y >= 326) return 0;
    if (x >= 960 && x < 1000) return -1;
    if (x >= 1208 && x < 1248) return 1;
    return 0;
}

DemoControl panel_hit_test(int x, int y)
{
    int i;
    y += 630;
    for (i = 0; i < DEMO_CONTROL_COUNT; ++i) {
        const ControlVisual *item = &controls[i];
        int dx = x - item->x;
        int dy = y - item->y;
        if (item->encoder ? dx * dx + dy * dy <= item->radius * item->radius :
                            dx >= -item->radius && dx < item->radius &&
                            dy >= -item->radius && dy < item->radius)
            return item->id;
    }
    return DEMO_CONTROL_COUNT;
}

void panel_draw(HDC target, const DemoSignal *demo,
                DemoControl pressed, DemoControl hovered, int hold_progress,
                int hold_time_ms)
{
    int i;
    wchar_t delay_label[40];
    POINT previous_origin;
    fill_rect(target, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, COL(15,26,33));
    outline(target, 9, 8, PANEL_WIDTH - 18, PANEL_HEIGHT - 17, COL(73,96,104));
    SetViewportOrgEx(target, 0, -630, &previous_origin);
    section(target, 32, 296, L"INPUTS");
    section(target, 334, 294, L"VERTICAL");
    section(target, 634, 140, L"HORIZONTAL");
    section(target, 779, 140, L"TRIGGER");
    section(target, 924, 140, L"FUNCTIONAL");
    section(target, 1069, 179, L"ACQUIRE");
    jack(target, 111, L"CHANNEL 1", COL(255,196,77));
    jack(target, 250, L"CHANNEL 2", COL(82,210,235));
    centered_ascii(target, 111, 868, 130, 21,
                   demo->screen.ch1_enabled ? demo->screen.ch1_input : "OFF",
                   label_font, COL(255,196,77));
    centered_ascii(target, 250, 868, 130, 21,
                   demo->screen.ch2_enabled ? demo->screen.ch2_input : "OFF",
                   label_font, COL(82,210,235));
    centered_ascii(target, 111, 893, 130, 21,
                   demo->screen.ch1_enabled ? demo->screen.ch1_scale : "",
                   hint_font, COL(167,185,191));
    centered_ascii(target, 250, 893, 130, 21,
                   demo->screen.ch2_enabled ? demo->screen.ch2_scale : "",
                   hint_font, COL(167,185,191));
    for (i = 0; i < DEMO_CONTROL_COUNT; ++i) {
        if (controls[i].encoder)
            draw_knob(target, &controls[i], demo, pressed, hovered);
        else
            draw_button(target, &controls[i], demo, pressed, hovered);
    }
    fill_rect(target, 408, 914, 2, 12, COL(105,139,151));
    fill_rect(target, 408, 924, 205, 2, COL(105,139,151));
    fill_rect(target, 789, 924, 205, 2, COL(105,139,151));
    fill_rect(target, 992, 914, 2, 12, COL(105,139,151));
    centered(target, 701, 914, 176, 18, L"SCREENSHOT + CSV",
             hint_font, COL(196,216,223));
    if (pressed != DEMO_CONTROL_COUNT && hold_progress >= 0) {
        const ControlVisual *control = &controls[pressed];
        int bar_y = control->encoder ? control->y + 23 : control->y + 13;
        int width = 56 * hold_progress / 100;
        fill_rect(target, control->x - 30, bar_y, 60, 7, COL(5,11,15));
        fill_rect(target, control->x - 28, bar_y + 2, width, 3,
                  hold_progress == 100 ? COL(112,223,163) : control->accent);
    }
    centered(target, 1159, 687, 166, 24, L"CAPTURE", label_font, COL(226,238,240));
    ellipse(target, 1159, 754, 23, COL(24,49,43), COL(61,91,78), 2);
    ellipse(target, 1159, 754, 12, demo->screen.running ? COL(112,223,163) : COL(64,83,76),
            COL(22,42,34), 2);
    if (pressed != DEMO_CONTROL_COUNT && hold_progress >= 0) {
        wchar_t hold_label[64];
        if (hold_progress == 100)
            swprintf(hold_label, 64, L"HOLD ACTIVE / RELEASE");
        else
            swprintf(hold_label, 64, L"HOLD %d%% / KEEP PRESSED", hold_progress);
        centered(target, 480, 936, 900, 20,
                 hold_label, hint_font, COL(112,223,163));
    } else {
        centered(target, 480, 936, 900, 20,
                 L"WHEEL: TURN    RELEASE: SHORT PRESS    HOLD: LONG PRESS",
                 hint_font, COL(146,169,177));
    }
    swprintf(delay_label, 40, L"LONG PRESS %.1f S", hold_time_ms / 1000.0);
    fill_rect(target, 960, 936, 40, 20, COL(38,55,64));
    outline(target, 960, 936, 40, 20, COL(97,124,135));
    centered(target, 980, 936, 40, 20, L"−", label_font, COL(228,239,241));
    centered(target, 1104, 936, 200, 20, delay_label, hint_font, COL(228,239,241));
    fill_rect(target, 1208, 936, 40, 20, COL(38,55,64));
    outline(target, 1208, 936, 40, 20, COL(97,124,135));
    centered(target, 1228, 936, 40, 20, L"+", label_font, COL(228,239,241));
    SetViewportOrgEx(target, previous_origin.x, previous_origin.y, NULL);
}
