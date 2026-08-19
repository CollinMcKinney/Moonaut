#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "window.h"
#include <stdio.h>
#include <math.h>    // for fabsf

#ifdef __cplusplus
extern "C" {
#endif

// --- Static state for axis change detection ---
#define MAX_GAMEPADS 4
#define AXIS_CHANGE_THRESHOLD 0.005f  // Minimum change to log

static float prev_axes[MAX_GAMEPADS][C89FW_GAMEPAD_AXIS_COUNT];
static int axis_state_initialized = 0;

// --- Engine-friendly input state (optional, but recommended) ---
typedef struct {
    // Mouse (mirrors C89FW_window_t)
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    double mouse_scroll_x, mouse_scroll_y;
    
    // Gamepad axes (mirrors C89FW_window_t)
    struct {
        float axes[C89FW_GAMEPAD_AXIS_COUNT];
        float axes_delta[C89FW_GAMEPAD_AXIS_COUNT];
        unsigned char connected;
    } gamepads[MAX_GAMEPADS];
} EngineInputState;

static EngineInputState g_input;

// --- Helper: Reset axis state (call on gamepad disconnect) ---
static void reset_axis_state(int gamepad_index) {
    for (int a = 0; a < C89FW_GAMEPAD_AXIS_COUNT; a++) {
        prev_axes[gamepad_index][a] = 0.0f;
        g_input.gamepads[gamepad_index].axes[a] = 0.0f;
        g_input.gamepads[gamepad_index].axes_delta[a] = 0.0f;
    }
    g_input.gamepads[gamepad_index].connected = 0;
}

// --- Main input processing function ---
static void input_process_events(C89FW_window_t *win)
{
    C89FW_event_t event;
    
    // --- PHASE 1: Process discrete events (buttons, resize, etc.) ---
    while (C89FW_poll_event(win, &event)) {
        switch (event.type) {
            case C89FW_EVENT_KEY_DOWN:
                printf("[INPUT] key pressed: key=%d\n", event.data.key.code);
                break;
                
            case C89FW_EVENT_KEY_UP:
                printf("[INPUT] key released: key=%d\n", event.data.key.code);
                break;
                
            case C89FW_EVENT_MOUSE_DOWN:
                printf("[INPUT] mouse button pressed: button=%d at (%d, %d)\n",
                       event.data.mouse_button.button, 
                       event.data.mouse_button.x, 
                       event.data.mouse_button.y);
                break;
                
            case C89FW_EVENT_MOUSE_UP:
                printf("[INPUT] mouse button released: button=%d at (%d, %d)\n",
                       event.data.mouse_button.button,
                       event.data.mouse_button.x, 
                       event.data.mouse_button.y);
                break;
                
            case C89FW_EVENT_MOUSE_MOVE:
                printf("[INPUT] mouse moved: (%d, %d) delta=(%d, %d)\n",
                       event.data.mouse_move.x, 
                       event.data.mouse_move.y,
                       event.data.mouse_move.delta_x, 
                       event.data.mouse_move.delta_y);
                break;
                
            case C89FW_EVENT_MOUSE_SCROLL:
                printf("[INPUT] mouse wheel: scroll=(%f, %f)\n",
                       event.data.mouse_scroll.delta_x, 
                       event.data.mouse_scroll.delta_y);
                break;
                
            case C89FW_EVENT_RESIZE:
                printf("[INPUT] window resized: (%d, %d)\n",
                       event.data.resize.width, 
                       event.data.resize.height);
                jobgraph_wait(g_jobgraph);
                window_resize(event.data.resize.width, event.data.resize.height);
                break;
                
            case C89FW_EVENT_CLOSE:
                printf("[INPUT] window close requested\n");
                break;
                
            case C89FW_EVENT_FOCUS_GAINED:
                printf("[INPUT] window focus gained\n");
                break;
                
            case C89FW_EVENT_FOCUS_LOST:
                printf("[INPUT] window focus lost\n");
                break;
                
            case C89FW_EVENT_GAMEPAD_CONNECT:
                printf("[INPUT] gamepad %d connected\n", 
                       event.data.gamepad_button.gamepad_index);
                g_input.gamepads[event.data.gamepad_button.gamepad_index].connected = 1;
                break;
                
            case C89FW_EVENT_GAMEPAD_DISCONNECT:
                printf("[INPUT] gamepad %d disconnected\n", 
                       event.data.gamepad_button.gamepad_index);
                reset_axis_state(event.data.gamepad_button.gamepad_index);
                break;
                
            case C89FW_EVENT_GAMEPAD_DOWN:
                printf("[INPUT] gamepad %d button %d pressed\n",
                       event.data.gamepad_button.gamepad_index, 
                       event.data.gamepad_button.button);
                break;
                
            case C89FW_EVENT_GAMEPAD_UP:
                printf("[INPUT] gamepad %d button %d released\n",
                       event.data.gamepad_button.gamepad_index, 
                       event.data.gamepad_button.button);
                break;
                
            default:
                break;
        }
    }
    
    // --- PHASE 2: Poll and log axis changes (like mouse move) ---
    for (int g = 0; g < MAX_GAMEPADS; g++) {
        // Only process if connected (or if it was previously connected)
        unsigned char connected = C89FW_gamepad_present(win, g);
        
        if (connected) {
            g_input.gamepads[g].connected = 1;
            
            // Read all axes in one go (performance + consistency)
            float axes[C89FW_GAMEPAD_AXIS_COUNT];
            axes[C89FW_GAMEPAD_AXIS_LEFT_X]  = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_LEFT_X);
            axes[C89FW_GAMEPAD_AXIS_LEFT_Y]  = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_LEFT_Y);
            axes[C89FW_GAMEPAD_AXIS_RIGHT_X] = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_RIGHT_X);
            axes[C89FW_GAMEPAD_AXIS_RIGHT_Y] = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_RIGHT_Y);
            axes[C89FW_GAMEPAD_AXIS_LEFT_TRIGGER]  = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_LEFT_TRIGGER);
            axes[C89FW_GAMEPAD_AXIS_RIGHT_TRIGGER] = C89FW_gamepad_axis(win, g, C89FW_GAMEPAD_AXIS_RIGHT_TRIGGER);
            
            // Store absolute values and compute deltas
            for (int a = 0; a < C89FW_GAMEPAD_AXIS_COUNT; a++) {
                float current = axes[a];
                float delta = current - prev_axes[g][a];
                
                // Store in engine state
                g_input.gamepads[g].axes[a] = current;
                g_input.gamepads[g].axes_delta[a] = delta;
                
                // Log significant changes (like mouse move events)
                if (fabsf(delta) > AXIS_CHANGE_THRESHOLD) {
                    // Map axis index to string for readable output
                    const char* axis_names[] = {
                        "LEFT_X", "LEFT_Y", "RIGHT_X", "RIGHT_Y", 
                        "LEFT_TRIGGER", "RIGHT_TRIGGER"
                    };
                    printf("[INPUT] gamepad %d axis %s changed: %.3f (delta=%.3f)\n",
                           g, axis_names[a], current, delta);
                }
                
                // Store for next frame
                prev_axes[g][a] = current;
            }
        } else {
            // If disconnected, reset state
            if (g_input.gamepads[g].connected) {
                reset_axis_state(g);
                printf("[INPUT] gamepad %d disconnected (polling)\n", g);
            }
        }
    }
}

// --- Accessor functions for engine state (optional) ---
static EngineInputState* input_get_state(void) {
    return &g_input;
}

static float input_get_axis(int gamepad, C89FW_gamepad_axis_t axis) {
    if (gamepad < 0 || gamepad >= MAX_GAMEPADS) return 0.0f;
    if (axis < 0 || axis >= C89FW_GAMEPAD_AXIS_COUNT) return 0.0f;
    return g_input.gamepads[gamepad].axes[axis];
}

static float input_get_axis_delta(int gamepad, C89FW_gamepad_axis_t axis) {
    if (gamepad < 0 || gamepad >= MAX_GAMEPADS) return 0.0f;
    if (axis < 0 || axis >= C89FW_GAMEPAD_AXIS_COUNT) return 0.0f;
    return g_input.gamepads[gamepad].axes_delta[axis];
}

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */