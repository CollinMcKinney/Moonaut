/*
 * main.c – Moonaut Engine demo (fixed 32-bit framebuffer)
 *
 * Compile (Windows):
 *   gcc -ansi -o engine.exe main.c src\lua.c -lm -lgdi32
 * Compile (Linux):
 *   gcc -ansi -o engine main.c src/lua.c -lm -lX11
 */

#include <stdio.h>
#include <stdlib.h>

/* ---------- platform windowing (must be before engine includes) ---------- */
#ifdef _WIN32
#  include <windows.h>
#elif defined(__linux__)
#  include <sys/time.h>
#endif

/* ---------- engine + Lua ---------- */
#define LUA_IMPLEMENTATION
#include "src/runtime.h"
#include "src/defaults.h"

/* ---------- FPS counter ---------- */
#include <time.h>

/* ======================================================================
   Windows window
   ====================================================================== */
#ifdef _WIN32
static HWND   hwnd_main;
static int    g_width, g_height;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void win32_init(const char *title, int w, int h) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = title;
    RegisterClass(&wc);

    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    hwnd_main = CreateWindowEx(0, title, title, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top,
                              NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hwnd_main, SW_SHOW);
    g_width = w;
    g_height = h;
}

static void win32_present(const u32 *fb) {
    HDC hdc = GetDC(hwnd_main);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = g_width;
    bmi.bmiHeader.biHeight      = -g_height;   /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;          /* <-- FIXED */
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage   = g_width * g_height * 4;

    SetDIBitsToDevice(
        hdc,
        0, 0, g_width, g_height,
        0, 0, 0, g_height,
        fb,
        &bmi,
        DIB_RGB_COLORS
    );

    ReleaseDC(hwnd_main, hdc);
}

static int win32_pump(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return 0;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 1;
}
#endif

/* ======================================================================
   Linux X11 window
   ====================================================================== */
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display *dpy;
static Window   win;
static GC       gc;
static XImage  *ximage;
static int      g_width, g_height;

static void x11_init(const char *title, int w, int h) {
    dpy = XOpenDisplay(NULL);
    int screen = DefaultScreen(dpy);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, w, h, 1, 0, 0);
    XStoreName(dpy, win, title);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);

    g_width = w;
    g_height = h;

    XFlush(dpy);
}

static void x11_present(const uint32_t *fb) {
    ximage = XCreateImage(
        dpy,
        DefaultVisual(dpy, DefaultScreen(dpy)),
        24,
        ZPixmap,
        0,
        (char*)fb,
        g_width,
        g_height,
        32,
        0
    );

    XPutImage(dpy, win, gc, ximage, 0, 0, 0, 0, g_width, g_height);
    XDestroyImage(ximage);
    XFlush(dpy);
}

static int x11_pump(void) {
    XEvent ev;
    while (XPending(dpy)) {
        XNextEvent(dpy, &ev);
        if (ev.type == KeyPress) return 0;
    }
    return 1;
}
#endif

/* ======================================================================
   FPS counter
   ====================================================================== */
static double app_time_seconds(void) {
    #ifdef _WIN32
        static LARGE_INTEGER freq;
        LARGE_INTEGER now;
        if (freq.QuadPart == 0) {
            QueryPerformanceFrequency(&freq);
        }
        QueryPerformanceCounter(&now);
        return (double)now.QuadPart / (double)freq.QuadPart;
    #elif defined(__linux__)
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    #else
        return (double)clock() / (double)CLOCKS_PER_SEC;
    #endif
}

static void print_fps(double now) {
    static double last = 0.0;
    static int frames = 0;

    if (last == 0.0) last = now;

    frames++;
    {
        double elapsed = now - last;
    if (elapsed >= 1.0) {
        printf("FPS: %d\n", (int)(frames / elapsed));
        frames = 0;
        last = now;
    }
    }
}

void window_init(const char *title, int w, int h) {
        /* create window */
    #ifdef _WIN32
        win32_init(title, w, h);
    #elif defined(__linux__)
        x11_init(title, w, h);
    #endif
}

int is_running(){
    int running;
    #ifdef _WIN32
            running = win32_pump();
    #elif defined(__linux__)
            running = x11_pump();
    #else
            running = 1;
    #endif
    return running;
}

void present_frame() {
    #ifdef _WIN32
        win32_present(fb);
    #elif defined(__linux__)
        x11_present(fb);
    #endif   
}

/* ======================================================================
   main
   ====================================================================== */
int main(void) {
    const int width  = 1600;
    const int height = 900;
    const double max_frame_time = 0.25;
    double last_time;
    double accumulator;

    window_init("Moonaut Engine", width, height);

    if (render_init(width, height) != 0) {
        fprintf(stderr, "Failed to initialise renderer\n");
        return 1;
    }

    tag_register_default_all();

    fprintf(stderr, "Registered defaults\n");

    scenario_world world;
    scenario_init(&world, width, height);
    last_time = app_time_seconds();
    accumulator = 0.0;

    while (1) {
        int running;
        real fixed_dt;
        double now;
        double frame_time;
        int step_count;

        running = is_running();
        if (!running) break;

        now = app_time_seconds();
        frame_time = now - last_time;
        last_time = now;
        if (frame_time < 0.0) frame_time = 0.0;
        if (frame_time > max_frame_time) frame_time = max_frame_time;

        fixed_dt = scenario_get_fixed_dt();
        accumulator += frame_time;
        step_count = 0;
        while (accumulator >= (double)fixed_dt) {
            scenario_update(&world, fixed_dt);
            accumulator -= (double)fixed_dt;
            step_count++;
            if (step_count >= 15) {
                accumulator = 0.0;
                break;
            }
        }

        render_set_time((real)now);
        scenario_render(&world);

        const u32 *fb = render_get_fb(); /* <-- FIXED TYPE */

        present_frame();

        print_fps(now);
    }

    scenario_shutdown(&world);
    render_shutdown();
    return 0;
}
