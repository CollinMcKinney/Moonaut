#ifndef WINDOW_H
#define WINDOW_H

/* RGFW configuration - X11 for Linux, with UNIX backend */
#define RGFW_X11
#define RGFW_UNIX
#define RGFW_IMPLEMENTATION
#define RGFW_NO_UNPREFIXED_INTS

#include "rasterizer.h"
#include "../libs/rgfw/RGFW.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window state tracked by the window system */
static RGFW_window *g_window = NULL;
static int g_window_inited = 0;

/* Expose window for input.c */
static RGFW_window* window_get(void) { return g_window; }

/* Initialize the window with RGFW
 * title: window title
 * w, h: width and height
 * returns: 0 on success, -1 on failure
 */
static int window_init(const char *title, int w, int h)
{
    g_window = RGFW_createWindow(title, 0, 0, w, h, RGFW_windowCenter | RGFW_windowFocus);
    if (!g_window) {
        fprintf(stderr, "Failed to create window\n");
        g_window_inited = 0;
        return -1;
    }
    RGFW_window_setExitKey(g_window, RGFW_keyNULL);
    g_window->internal.lastMouseX = w / 2;
    g_window->internal.lastMouseY = h / 2;
    g_window_inited = 1;
    return 0;
}

/* Check if the window should still be running (process events)
 * returns: 1 if running, 0 if should close
 */
static int is_running(void)
{
    if (!g_window_inited || !g_window) return 0;
    return !RGFW_window_shouldClose(g_window);
}

/* Present the framebuffer to the window using RGFW surface blit
 * Uses fb_front from rasterizer.h (32-bit packed pixels)
 * The rasterizer uses format: (r << 16) | (g << 8) | b - BGR in memory
 */
static void present_frame(const u32 *fb)
{
    if (!g_window || !fb) return;
    
    extern i32 fw, fh;
    int w = fw;
    int h = fh;
    
    RGFW_surface *surface = RGFW_window_createSurface(g_window, (u8*)fb, w, h, RGFW_formatBGRA8);
    if (surface) {
        RGFW_window_blitSurface(g_window, surface);
        RGFW_surface_free(surface);
    }
}

/* Cleanup window resources */
static void window_shutdown(void)
{
    if (g_window) {
        RGFW_window_close(g_window);
        g_window = NULL;
    }
    g_window_inited = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */