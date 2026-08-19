#ifndef WINDOW_H
#define WINDOW_H

#define C89FW_IMPLEMENTATION
#include "../libs/C89FW/C89FW.h"

#include "rasterizer/rasterizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window state tracked by the window system */
static C89FW_window_t *g_window = NULL;
static int g_window_inited = 0;

/* Expose window for input.c */
static C89FW_window_t* window_get(void) { return g_window; }

/* Initialize the window with C89FW */
static int window_init(const char *title, int w, int h)
{
    g_window = (C89FW_window_t*)malloc(sizeof(C89FW_window_t));
    if (!g_window) {
        fprintf(stderr, "Failed to allocate window\n");
        return -1;
    }
    
    if (!C89FW_open(g_window, w, h, title)) {
        fprintf(stderr, "Failed to create window\n");
        free(g_window);
        g_window = NULL;
        g_window_inited = 0;
        return -1;
    }
    
    g_window_inited = 1;
    return 0;
}

static int is_running(void)
{
    if (!g_window_inited || !g_window) return 0;
    
    if (!C89FW_update(g_window)) {
        return 0;
    }
    
    return !g_window->should_close;
}

static void present_frame(void *fb)
{
    if (!g_window || !fb) return;
    g_window->framebuffer = (unsigned char*)fb;
    C89FW_present(g_window);
}

static void window_shutdown(void)
{
    if (g_window) {
        C89FW_close(g_window);
        free(g_window);
        g_window = NULL;
    }
    g_window_inited = 0;
}

static void window_resize(int new_w, int new_h)
{
    /* Resize the renderer first */
    render_resize(new_w, new_h);
    /* Tell C89FW the resize is complete - this updates window->width/height */
    C89FW_apply_resize(g_window);
}

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */
