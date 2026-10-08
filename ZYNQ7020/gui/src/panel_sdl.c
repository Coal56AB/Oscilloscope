#include "panel_sdl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COL(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

typedef struct {
    DemoControl id;
    int x, y, radius;
    const char *label;
    const char *hint;
    uint32_t accent;
    int encoder;
} ControlVisual;

/* Coordinates match the Win32 control panel; y is relative to its window. */
static const ControlVisual controls[] = {
    {DEMO_ENC_CH1, 409,124,48,"CH1","V/DIV / POSITION",COL(255,196,77),1},
    {DEMO_ENC_CH2, 554,124,48,"CH2","V/DIV / POSITION",COL(82,210,235),1},
    {DEMO_ENC_TIME,704,124,48,"TIME","TIME / POSITION",COL(195,211,219),1},
    {DEMO_ENC_TRIGGER,849,124,48,"TRIGGER","PREVIEW / APPLY",COL(112,223,163),1},
    {DEMO_ENC_FUNCTION,994,124,48,"FUNCTION","HOLD: FINE",COL(195,211,219),1},
    {DEMO_BTN_CH1,409,235,23,"EN CH1","ON / MENU / 2X OFF",COL(255,196,77),0},
    {DEMO_BTN_CH2,554,235,23,"EN CH2","ON / MENU / 2X OFF",COL(82,210,235),0},
    {DEMO_BTN_CURSOR,704,235,23,"CURSOR","HOLD: SETTINGS",COL(195,172,255),0},
    {DEMO_BTN_MODE,849,235,23,"TRG MODE","HOLD: SETTINGS",COL(112,223,163),0},
    {DEMO_BTN_MENU,994,235,23,"MENU / BACK","HOLD: ZOOM",COL(195,211,219),0},
    {DEMO_BTN_RUN,1159,211,23,"RUN / STOP","HOLD: FORCE TRIG",COL(112,223,163),0}
};

static uint32_t *frame;

static const uint8_t glyphs[51][7] = {
    {0,0,0,0,0,0,0},{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{0,0,0,0,0,0,4},
    {0,4,0,0,4,0,0},{0,0,0,31,0,0,0},{0,0,4,14,4,0,0},
    {1,2,2,4,8,8,16},{0,0,17,10,4,10,17},{0,0,17,10,4,10,17},
    {0,0,0,0,0,0,31},{0,0,17,17,17,19,13},{0,0,15,16,14,1,30},
    {16,16,18,20,24,20,18},{0,0,31,2,4,8,31},{0,0,26,21,21,21,21},
    {25,26,4,8,11,19,0}
};

static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return 1 + c - '0';
    if (c >= 'A' && c <= 'Z') return 11 + c - 'A';
    switch (c) {
        case '.': return 37; case ':': return 38; case '-': return 39;
        case '+': return 40; case '/': return 41; case '*': return 42;
        case '_': return 44; case '%': return 50; default: return 0;
    }
}

static void pixel(int x, int y, uint32_t color)
{
    if ((unsigned)x < PANEL_WIDTH && (unsigned)y < PANEL_HEIGHT)
        frame[(size_t)y * PANEL_WIDTH + x] = color;
}

static void fill(int x, int y, int w, int h, uint32_t color)
{
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PANEL_WIDTH) w = PANEL_WIDTH - x;
    if (y + h > PANEL_HEIGHT) h = PANEL_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; ++yy)
        for (xx = x; xx < x + w; ++xx)
            frame[(size_t)yy * PANEL_WIDTH + xx] = color;
}

static void outline(int x, int y, int w, int h, uint32_t color)
{
    fill(x,y,w,1,color); fill(x,y+h-1,w,1,color);
    fill(x,y,1,h,color); fill(x+w-1,y,1,h,color);
}

static void line(int x0, int y0, int x1, int y1, uint32_t color, int width)
{
    int dx = abs(x1-x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        fill(x0-width/2,y0-width/2,width,width,color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void circle(int cx, int cy, int radius, uint32_t color)
{
    int x, y;
    for (y = -radius; y <= radius; ++y)
        for (x = -radius; x <= radius; ++x)
            if (x*x + y*y <= radius*radius) pixel(cx+x,cy+y,color);
}

static int text_width(const char *value, int scale)
{
    return value ? (int)strlen(value) * 6 * scale - scale : 0;
}

static void text(int x, int y, const char *value, int scale, uint32_t color)
{
    if (!value) return;
    while (*value) {
        const uint8_t *g = glyphs[glyph_index(*value++)];
        int row, col;
        for (row=0; row<7; ++row)
            for (col=0; col<5; ++col)
                if (g[row] & (16 >> col))
                    fill(x+col*scale,y+row*scale,scale,scale,color);
        x += 6*scale;
    }
}

static void centered(int cx, int y, const char *value, int scale, uint32_t color)
{
    text(cx-text_width(value,scale)/2,y,value,scale,color);
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
    if (id == DEMO_ENC_CH1) return demo->scale_index[0]*25 + demo->channel_position[0]/4;
    if (id == DEMO_ENC_CH2) return demo->scale_index[1]*25 + demo->channel_position[1]/4;
    if (id == DEMO_ENC_TIME) return demo->time_index*20 + demo->time_position/32;
    if (id == DEMO_ENC_TRIGGER) return demo->screen.trigger_preview_y/4;
    return demo->screen.menu_selected*22 + demo->screen.cursor_a/20;
}

static void section(int x, int width, const char *title)
{
    fill(x,17,width,286,COL(22,38,47)); outline(x,17,width,286,COL(64,87,96));
    fill(x+1,18,width-2,35,COL(43,63,72)); centered(x+width/2,27,title,2,COL(227,238,241));
}

static void jack(int x, const char *name, uint32_t accent)
{
    centered(x,61,name,2,COL(222,235,238));
    circle(x,131,49,accent); circle(x,131,43,COL(192,202,202));
    circle(x,131,36,COL(61,76,78)); circle(x,131,23,COL(207,215,215));
    circle(x,131,9,COL(24,35,39)); centered(x,206,"BNC INPUT",1,COL(149,171,180));
}

static void draw_knob(const ControlVisual *c, const DemoSignal *demo,
                      DemoControl pressed, DemoControl hovered)
{
    double a = (-100.0 + control_angle(demo,c->id))*3.14159265358979323846/180.0;
    circle(c->x,c->y,48,c->accent);
    circle(c->x,c->y,hovered==c->id?42:44,COL(8,15,19));
    circle(c->x,c->y,39,pressed==c->id?COL(58,75,81):COL(27,39,44));
    circle(c->x-8,c->y-9,14,COL(37,51,55));
    line(c->x+(int)(24*cos(a)),c->y+(int)(24*sin(a)),
         c->x+(int)(35*cos(a)),c->y+(int)(35*sin(a)),COL(231,242,242),4);
    centered(c->x,54,c->label,2,COL(226,238,240));
    centered(c->x,174,c->id==DEMO_ENC_FUNCTION && demo->screen.menu_open?
             "HOLD: BACK":c->hint,1,COL(149,171,180));
}

static void draw_button(const ControlVisual *c, const DemoSignal *demo,
                        DemoControl pressed, DemoControl hovered)
{
    uint32_t border = control_active(demo,c->id)?c->accent:COL(85,105,113);
    fill(c->x-c->radius,c->y-c->radius,c->radius*2,c->radius*2,border);
    fill(c->x-c->radius+4,c->y-c->radius+4,c->radius*2-8,c->radius*2-8,
         pressed==c->id?COL(67,86,92):hovered==c->id?COL(42,58,64):COL(24,34,39));
    centered(c->x,c->id==DEMO_BTN_RUN?158:191,
             c->id==DEMO_BTN_RUN && demo->waveform_loaded?"CSV":c->label,2,COL(228,239,241));
    centered(c->x,c->id==DEMO_BTN_RUN?250:261,
             c->id==DEMO_BTN_RUN && demo->waveform_loaded?"RETURN TO CAPTURE":c->hint,
             1,COL(149,171,180));
}

int panel_sdl_is_encoder(DemoControl control)
{
    return control >= DEMO_ENC_CH1 && control <= DEMO_ENC_FUNCTION;
}

int panel_sdl_hold_adjustment(int x, int y)
{
    if (y < 306 || y >= 326) return 0;
    if (x >= 960 && x < 1000) return -1;
    if (x >= 1208 && x < 1248) return 1;
    return 0;
}

DemoControl panel_sdl_hit_test(int x, int y)
{
    int i;
    for (i=0;i<DEMO_CONTROL_COUNT;++i) {
        const ControlVisual *c=&controls[i]; int dx=x-c->x,dy=y-c->y;
        if (c->encoder?dx*dx+dy*dy<=c->radius*c->radius:
            dx>=-c->radius&&dx<c->radius&&dy>=-c->radius&&dy<c->radius)
            return c->id;
    }
    return DEMO_CONTROL_COUNT;
}

void panel_sdl_draw(uint32_t *pixels, const DemoSignal *demo,
                    DemoControl pressed, DemoControl hovered,
                    int hold_progress, int hold_time_ms)
{
    int i; char label[48];
    frame=pixels; fill(0,0,PANEL_WIDTH,PANEL_HEIGHT,COL(15,26,33));
    outline(9,8,PANEL_WIDTH-18,PANEL_HEIGHT-17,COL(73,96,104));
    section(32,296,"INPUTS"); section(334,294,"VERTICAL");
    section(634,140,"HORIZONTAL"); section(779,140,"TRIGGER");
    section(924,140,"FUNCTIONAL"); section(1069,179,"ACQUIRE");
    jack(111,"CHANNEL 1",COL(255,196,77)); jack(250,"CHANNEL 2",COL(82,210,235));
    centered(111,238,demo->screen.ch1_enabled?demo->screen.ch1_input:"OFF",2,COL(255,196,77));
    centered(250,238,demo->screen.ch2_enabled?demo->screen.ch2_input:"OFF",2,COL(82,210,235));
    centered(111,263,demo->screen.ch1_enabled?demo->screen.ch1_scale:"",1,COL(167,185,191));
    centered(250,263,demo->screen.ch2_enabled?demo->screen.ch2_scale:"",1,COL(167,185,191));
    for(i=0;i<DEMO_CONTROL_COUNT;++i)
        if(controls[i].encoder) draw_knob(&controls[i],demo,pressed,hovered);
        else draw_button(&controls[i],demo,pressed,hovered);
    centered(701,284,"SCREENSHOT + CSV",1,COL(196,216,223));
    circle(1159,124,23,COL(24,49,43));
    circle(1159,124,12,demo->screen.running?COL(112,223,163):COL(64,83,76));
    if(pressed!=DEMO_CONTROL_COUNT&&hold_progress>=0) {
        const ControlVisual *c=&controls[pressed]; int by=c->y+(c->encoder?23:13);
        fill(c->x-30,by,60,7,COL(5,11,15));
        fill(c->x-28,by+2,56*hold_progress/100,3,
             hold_progress==100?COL(112,223,163):c->accent);
        snprintf(label,sizeof(label),hold_progress==100?"HOLD ACTIVE / RELEASE":"HOLD %d%% / KEEP PRESSED",hold_progress);
        centered(480,306,label,1,COL(112,223,163));
    } else centered(480,306,"WHEEL: TURN   RELEASE: SHORT PRESS   HOLD: LONG PRESS",1,COL(146,169,177));
    snprintf(label,sizeof(label),"LONG PRESS %.1f S",hold_time_ms/1000.0);
    fill(960,306,40,20,COL(38,55,64)); outline(960,306,40,20,COL(97,124,135));
    centered(980,309,"-",2,COL(228,239,241)); centered(1104,309,label,1,COL(228,239,241));
    fill(1208,306,40,20,COL(38,55,64)); outline(1208,306,40,20,COL(97,124,135));
    centered(1228,309,"+",2,COL(228,239,241));
}
