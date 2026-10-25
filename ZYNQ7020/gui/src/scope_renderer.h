#ifndef SCOPE_RENDERER_H
#define SCOPE_RENDERER_H
#include "scope_screen.h"
#include <stddef.h>

typedef struct ScopeRenderer ScopeRenderer;
typedef void (*ScopeRenderLease)(const void *,void *);
typedef struct {
    const void *data; size_t bytes;
    /* Immutable externally owned data can be pinned instead of copied. */
    ScopeRenderLease retain,release;
    void *context;
} ScopeRenderRegion;
typedef void (*ScopeRenderFunction)(uint32_t *,int,const ScopeScreen *);
typedef int (*ScopeRenderOutput)(const uint32_t *,void *);
/* One UI producer submits immutable copies; one worker renders the latest view.
   Every mutable object referenced by the screen must belong to a supplied region.
   Static strings/callbacks remain shared. No waiting for drawing in submit/take. */
ScopeRenderer *scope_renderer_create(size_t snapshot_bytes,ScopeRenderFunction draw);
/* Paint channels on a separate worker. Poll from the UI loop and invalidate
   when it returns nonzero; controls compose over the latest completed plot. */
int scope_renderer_enable_plot_layer(ScopeRenderer *renderer,ScopeRenderFunction draw);
int scope_renderer_poll_plot(ScopeRenderer *renderer);
int scope_renderer_flush_plot(ScopeRenderer *renderer,unsigned timeout_ms);
/* A plot is acknowledged only after display output, never after painting alone.
   Windowed frontends call acknowledge after presenting pixels from take(). */
uint64_t scope_renderer_presented_plot(ScopeRenderer *renderer);
void scope_renderer_acknowledge(ScopeRenderer *renderer);
/* Optional display output runs on the same worker. Set before first submit.
   Return 1 when presented, 0 to retry, negative on a device error. */
void scope_renderer_set_output(ScopeRenderer *renderer,ScopeRenderOutput output,void *context);
int scope_renderer_failed(ScopeRenderer *renderer);
uint64_t scope_renderer_submit(ScopeRenderer *renderer,const ScopeScreen *screen,
                               const ScopeRenderRegion *regions,unsigned count);
uint64_t scope_renderer_submit_overlay(ScopeRenderer *renderer,const ScopeScreen *screen,
                                       const ScopeRenderRegion *regions,unsigned count);
uint64_t scope_renderer_take(ScopeRenderer *renderer,uint32_t *pixels);
int scope_renderer_flush(ScopeRenderer *renderer,uint64_t generation,uint32_t *pixels,unsigned timeout_ms);
void scope_renderer_metrics(ScopeRenderer *renderer,uint64_t *frames,uint64_t *nanoseconds,uint64_t *output_nanoseconds);
void scope_renderer_destroy(ScopeRenderer *renderer);
#endif
