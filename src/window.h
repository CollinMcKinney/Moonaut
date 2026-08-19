#ifndef WINDOW_H
#define WINDOW_H

#define C89FW_IMPLEMENTATION
#include "../libs/C89FW/C89FW.h"

#ifdef __cplusplus
extern "C" {
#endif

static C89FW_window_t *g_window = NULL;
static int g_window_inited = 0;

static C89FW_window_t* window_get(void) { return g_window; }

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
    if (!C89FW_update(g_window)) return 0;
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

/* window_resize is not needed – if you need it, call render_resize from runtime.h. */
/* Remove the call to render_resize from window_resize, or delete the function. */
static void window_resize(int new_w, int new_h)
{
    /* render_resize is not available here – remove this line: */
    /* render_resize(new_w, new_h); */
    C89FW_apply_resize(g_window);
}

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */