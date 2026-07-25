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

/* ---------- engine + Lua ---------- */
#define LUA_IMPLEMENTATION
#include "src/clock.h"
#include "src/runtime.h"
#include "src/defaults.h"

/* ---------- window management (C89FW) ---------- */
#include "src/window.h"
#include "src/input.h"

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
    const int window_size = 4;
    const int width  = 256 * window_size;
    const int height = 144 * window_size;
    const u32 *fb = NULL;
    const double max_frame_time = 0.25;
    double last_time;
    double accumulator;
    scenario_world world;
    
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

    clock_init();
    scenario_init(&world, width, height);
    last_time = clock_monotonic();
    accumulator = 0.0;

    while (is_running()) {
        real fixed_dt;
        double now;
        double frame_time;
        i32 step_count;

        input_process_events(window_get(), &world);

        now = clock_monotonic();
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

        fb = render_get_fb();

        present_frame((void*)fb);


        print_fps(now);
    }

    scenario_shutdown(&world);
    render_shutdown();
    window_shutdown();

    return 0;
}
