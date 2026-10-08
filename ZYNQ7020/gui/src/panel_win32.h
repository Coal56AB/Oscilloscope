#ifndef PANEL_WIN32_H
#define PANEL_WIN32_H

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "demo_signal.h"

#define PANEL_WIDTH 1280
#define PANEL_HEIGHT 344

int panel_init(void);
void panel_cleanup(void);
void panel_draw(HDC target, const DemoSignal *demo,
                DemoControl pressed, DemoControl hovered, int hold_progress,
                int hold_time_ms);
DemoControl panel_hit_test(int x, int y);
int panel_is_encoder(DemoControl control);
int panel_hold_adjustment(int x, int y);

#endif
