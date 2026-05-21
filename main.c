/*
 * main.c – Moonaut Engine demo (fixed 32-bit framebuffer)
 *
 * Compile (Windows):
 *   gcc -std=gnu99 -o engine.exe main.c src\lua.c -lm -lgdi32
 * Compile (Linux):
 *   gcc -std=gnu99 -o engine main.c src/lua.c -lm -lX11
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifdef __linux__
#include <sys/time.h>
#endif

/* ---------- engine + Lua ---------- */
#define LUA_IMPLEMENTATION
#include "src/runtime.h"
#include "src/defaults.h"

/* ---------- window management (RGFW) ---------- */
#include "src/window.h"

/* ======================================================================
   FPS counter
   ====================================================================== */
static double app_time_seconds(void)
{
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

static void print_fps(double now)
{
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

/* ======================================================================
   main
   ====================================================================== */
int main(void)
{
    const int window_size = 5;
    const int width  = 256 * window_size;
    const int height = 144 * window_size;
    const double max_frame_time = 0.25;
    double last_time;
    double accumulator;

    if (window_init("Moonaut Engine", width, height) != 0) {
        fprintf(stderr, "Failed to initialise window\n");
        return 1;
    }

    if (render_init(width, height) != 0) {
        fprintf(stderr, "Failed to initialise renderer\n");
        window_shutdown();
        return 1;
    }

    tag_register_default_all();

    scenario_world world;
    scenario_init(&world, width, height);
    last_time = app_time_seconds();
    accumulator = 0.0;

    while (is_running()) {
        real fixed_dt;
        double now;
        double frame_time;
        i32 step_count;

        window_process_events();

        now = app_time_seconds();
        frame_time = now - last_time;
        last_time = now;
        if (frame_time < 0.0) frame_time = 0.0;
        if (frame_time > max_frame_time) frame_time = max_frame_time;

        fixed_dt = scenario_get_fixed_dt();
        accumulator += frame_time;
        step_count = 0;
        while (accumulator >= fixed_dt) {
            scenario_update(&world, fixed_dt);
            accumulator -= fixed_dt;
            step_count++;
            if (step_count >= 15) {
                accumulator = 0.0;
                break;
            }
        }

        render_set_time((real)now);
        scenario_render(&world);

        const u32 *fb = render_get_fb();
        present_frame(fb);

        print_fps(now);
    }

    scenario_shutdown(&world);
    render_shutdown();
    window_shutdown();

    return 0;
}