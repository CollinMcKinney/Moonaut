#ifndef WINDOW_H
#define WINDOW_H

/* RGFW configuration - X11 for Linux, with UNIX backend */
#define RGFW_IMPLEMENTATION
#define RGFW_INT_DEFINED /* avoid using RGFW u8, i32, etc. implementations. */

#include "rasterizer.h"
#include "../libs/rgfw/RGFW.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window state tracked by the window system */
static RGFW_window *g_window = NULL;
static int g_window_inited = 0;
static RGFW_surface *g_surface = NULL; /* Reusable surface for presentation */

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
    
    /* Create reusable surface for presentation (avoids per-frame XImage allocation) */
    g_surface = RGFW_window_createSurface(g_window, NULL, w, h, RGFW_formatBGRA8);
    
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
 * The rasterizer uses format: (r << 16) | (g << 8) | b - BGR ordered in u32 (B in low byte)
 * 
 * Note: For X11, we reuse the surface (g_surface) created at init time.
 * The surface's data pointer is updated each frame, avoiding XImage allocation.
 */
static void present_frame(const u32 *fb)
{
    if (!g_window || !fb) return;
    
    extern i32 fw, fh;
    int w = fw;
    int h = fh;
    
    /* Ensure surface exists (may have been freed during resize) */
    if (!g_surface) {
        g_surface = RGFW_window_createSurface(g_window, NULL, w, h, RGFW_formatBGRA8);
    }
    
    if (g_surface) {
        /* Update surface data pointer to current framebuffer without reallocating */
        /* Format is 0x00BBGGRR (BGRA with 0 alpha), which matches RGFW_formatBGRA8 layout */
        g_surface->data = (u8*)fb;
        /* Width/height may change on resize */
        g_surface->w = w;
        g_surface->h = h;
        RGFW_window_blitSurface(g_window, g_surface);
    }
}

/* Cleanup window resources */
static void window_shutdown(void)
{
    if (g_surface) {
        RGFW_surface_free(g_surface);
        g_surface = NULL;
    }
    if (g_window) {
        RGFW_window_close(g_window);
        g_window = NULL;
    }
    g_window_inited = 0;
}

/* Resize window and framebuffer - called on RGFW_windowResized event */
static void window_resize(int new_w, int new_h)
{
    /* Recreate surface with new dimensions since XImage has fixed size */
    RGFW_surface *old_surface = g_surface;
    
    render_resize(new_w, new_h);
    
    if (old_surface) {
        RGFW_surface_free(old_surface);
    }
    
    /* Recreate surface with new dimensions */
    g_surface = RGFW_window_createSurface(g_window, NULL, new_w, new_h, RGFW_formatBGRA8);
    if (!g_surface) {
        fprintf(stderr, "Warning: Failed to recreate surface after resize\n");
    }
}

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */