#ifndef PANEL_SDL_H
#define PANEL_SDL_H

#include <stdint.h>

#include "demo_signal.h"

#define PANEL_WIDTH 1280
#define PANEL_HEIGHT 344

void panel_sdl_draw(uint32_t *pixels, const DemoSignal *demo,
                    DemoControl pressed, DemoControl hovered,
                    int hold_progress, int hold_time_ms);
DemoControl panel_sdl_hit_test(int x, int y);
int panel_sdl_is_encoder(DemoControl control);
int panel_sdl_hold_adjustment(int x, int y);

#endif
