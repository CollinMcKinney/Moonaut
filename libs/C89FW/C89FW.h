/*
 * C89FW.h - v1.0.0
 *
 * Single-header cross-platform window & input library.
 * Written in ANSI C89 / Objective-C (macOS).
 *
 * Provides:
 *   - Window creation (Windows, Linux/X11, macOS)
 *   - Input (keyboard, mouse, gamepad)
 *   - Software framebuffer rendering (optional)
 *   - Native handle getter (for attaching external rendering APIs)
 *
 * EXTENSIBILITY: Use C89FW_get_native_handles() to get raw OS handles
 * (HWND, X11 Window, NSView) and attach any rendering backend
 * (OpenGL, Vulkan, DirectX, Metal, console SDKs) externally.
 *
 * Usage (Software):
 *   #define C89FW_IMPLEMENTATION
 *   #include "C89FW.h"
 *
 *   C89FW_window_t win;
 *   C89FW_open(&win, 800, 600, "Software Renderer");
 *   unsigned char* fb = (unsigned char*)malloc(800*600*4);
 *   C89FW_set_framebuffer(&win, fb, 800, 600);
 *
 *   while (!win.should_close) {
 *       C89FW_update(&win);
 *       // Draw to framebuffer...
 *       C89FW_present(&win);
 *   }
 *   free(fb);
 *   C89FW_close(&win);
 */

#ifndef C89FW_H
#define C89FW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ---------- Platform Detection ---------- */
#if defined(_WIN32)
    #define C89FW_WINDOWS
#elif defined(__APPLE__)
    #define C89FW_MACOS
#elif defined(__linux__) || defined(__unix__)
    #define C89FW_LINUX
#else
    #error "C89FW supports Windows, macOS, and Linux"
#endif

/* ---------- Minimum Window Size ---------- */
#define C89FW_MIN_WIDTH  256
#define C89FW_MIN_HEIGHT 144

/* ---------- Input Enums ---------- */
typedef enum {
    C89FW_KEY_UNKNOWN = 0,
    C89FW_KEY_ESCAPE,
    C89FW_KEY_ENTER,
    C89FW_KEY_SPACE,
    C89FW_KEY_BACKSPACE,
    C89FW_KEY_TAB,
    C89FW_KEY_SHIFT,
    C89FW_KEY_CONTROL,
    C89FW_KEY_ALT,
    C89FW_KEY_CAPS_LOCK,
    C89FW_KEY_F1,  C89FW_KEY_F2,  C89FW_KEY_F3,  C89FW_KEY_F4,
    C89FW_KEY_F5,  C89FW_KEY_F6,  C89FW_KEY_F7,  C89FW_KEY_F8,
    C89FW_KEY_F9,  C89FW_KEY_F10, C89FW_KEY_F11, C89FW_KEY_F12,
    C89FW_KEY_LEFT, C89FW_KEY_RIGHT, C89FW_KEY_UP, C89FW_KEY_DOWN,
    C89FW_KEY_A, C89FW_KEY_B, C89FW_KEY_C, C89FW_KEY_D, C89FW_KEY_E,
    C89FW_KEY_F, C89FW_KEY_G, C89FW_KEY_H, C89FW_KEY_I, C89FW_KEY_J,
    C89FW_KEY_K, C89FW_KEY_L, C89FW_KEY_M, C89FW_KEY_N, C89FW_KEY_O,
    C89FW_KEY_P, C89FW_KEY_Q, C89FW_KEY_R, C89FW_KEY_S, C89FW_KEY_T,
    C89FW_KEY_U, C89FW_KEY_V, C89FW_KEY_W, C89FW_KEY_X, C89FW_KEY_Y, C89FW_KEY_Z,
    C89FW_KEY_0, C89FW_KEY_1, C89FW_KEY_2, C89FW_KEY_3, C89FW_KEY_4,
    C89FW_KEY_5, C89FW_KEY_6, C89FW_KEY_7, C89FW_KEY_8, C89FW_KEY_9,
    C89FW_KEY_KP_0, C89FW_KEY_KP_1, C89FW_KEY_KP_2, C89FW_KEY_KP_3, C89FW_KEY_KP_4,
    C89FW_KEY_KP_5, C89FW_KEY_KP_6, C89FW_KEY_KP_7, C89FW_KEY_KP_8, C89FW_KEY_KP_9,
    C89FW_KEY_COUNT
} C89FW_key_t;

typedef enum {
    C89FW_MOUSE_LEFT = 0,
    C89FW_MOUSE_MIDDLE,
    C89FW_MOUSE_RIGHT,
    C89FW_MOUSE_BUTTON_COUNT
} C89FW_mouse_button_t;

typedef enum {
    C89FW_GAMEPAD_A = 0,
    C89FW_GAMEPAD_B,
    C89FW_GAMEPAD_X,
    C89FW_GAMEPAD_Y,
    C89FW_GAMEPAD_LEFT_BUMPER,
    C89FW_GAMEPAD_RIGHT_BUMPER,
    C89FW_GAMEPAD_BACK,
    C89FW_GAMEPAD_START,
    C89FW_GAMEPAD_GUIDE,
    C89FW_GAMEPAD_LEFT_THUMB,
    C89FW_GAMEPAD_RIGHT_THUMB,
    C89FW_GAMEPAD_DPAD_UP,
    C89FW_GAMEPAD_DPAD_RIGHT,
    C89FW_GAMEPAD_DPAD_DOWN,
    C89FW_GAMEPAD_DPAD_LEFT,
    C89FW_GAMEPAD_BUTTON_COUNT
} C89FW_gamepad_button_t;

typedef enum {
    C89FW_GAMEPAD_AXIS_LEFT_X = 0,
    C89FW_GAMEPAD_AXIS_LEFT_Y,
    C89FW_GAMEPAD_AXIS_RIGHT_X,
    C89FW_GAMEPAD_AXIS_RIGHT_Y,
    C89FW_GAMEPAD_AXIS_LEFT_TRIGGER,
    C89FW_GAMEPAD_AXIS_RIGHT_TRIGGER,
    C89FW_GAMEPAD_AXIS_COUNT
} C89FW_gamepad_axis_t;

/* ---------- Event System ---------- */
typedef enum {
    C89FW_EVENT_NONE = 0,
    C89FW_EVENT_KEY_DOWN,
    C89FW_EVENT_KEY_UP,
    C89FW_EVENT_MOUSE_DOWN,
    C89FW_EVENT_MOUSE_UP,
    C89FW_EVENT_MOUSE_MOVE,
    C89FW_EVENT_MOUSE_SCROLL,
    C89FW_EVENT_GAMEPAD_CONNECT,
    C89FW_EVENT_GAMEPAD_DISCONNECT,
    C89FW_EVENT_GAMEPAD_DOWN,
    C89FW_EVENT_GAMEPAD_UP,
    C89FW_EVENT_RESIZE,
    C89FW_EVENT_CLOSE,
    C89FW_EVENT_FOCUS_GAINED,
    C89FW_EVENT_FOCUS_LOST
} C89FW_event_type_t;

typedef struct { C89FW_key_t code; } C89FW_key_event_data_t;
typedef struct { C89FW_mouse_button_t button; int x, y; } C89FW_mouse_button_event_data_t;
typedef struct { int x, y; int delta_x, delta_y; } C89FW_mouse_move_event_data_t;
typedef struct { double delta_x, delta_y; } C89FW_mouse_scroll_event_data_t;
typedef struct { int gamepad_index; C89FW_gamepad_button_t button; } C89FW_gamepad_button_event_data_t;
typedef struct { int width, height; } C89FW_resize_event_data_t;

typedef struct {
    C89FW_event_type_t type;
    double timestamp;
    union {
        C89FW_key_event_data_t key;
        C89FW_mouse_button_event_data_t mouse_button;
        C89FW_mouse_move_event_data_t mouse_move;
        C89FW_mouse_scroll_event_data_t mouse_scroll;
        C89FW_gamepad_button_event_data_t gamepad_button;
        C89FW_resize_event_data_t resize;
    } data;
} C89FW_event_t;

/* ========================================================================
   WINDOW STRUCT
   ======================================================================== */
typedef struct C89FW_window_t {
    void* internal;                 /* Platform-specific data */
    int width;
    int height;
    int should_close;
    double time;
    double delta_time;
    int has_focus;

    /* Input state */
    unsigned char keys[C89FW_KEY_COUNT];
    unsigned char keys_previous[C89FW_KEY_COUNT];
    unsigned char mouse_buttons[C89FW_MOUSE_BUTTON_COUNT];
    unsigned char mouse_buttons_previous[C89FW_MOUSE_BUTTON_COUNT];
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    double mouse_scroll_x, mouse_scroll_y;
    struct {
        unsigned char present;
        unsigned char buttons[C89FW_GAMEPAD_BUTTON_COUNT];
        unsigned char buttons_previous[C89FW_GAMEPAD_BUTTON_COUNT];
        float axes[C89FW_GAMEPAD_AXIS_COUNT];
    } gamepads[4];

    /* Software framebuffer (optional) */
    unsigned char* framebuffer;
    int framebuffer_owned;

    void* event_queue_internal;
} C89FW_window_t;

/* ========================================================================
   NATIVE HANDLES – THE EXTENSIBILITY BRIDGE
   ======================================================================== */
typedef struct C89FW_native_handles_t {
#if defined(C89FW_WINDOWS)
    void* hwnd;          /* HWND */
#elif defined(C89FW_LINUX)
    void* display;       /* Display* */
    unsigned long window;/* X11 Window */
#elif defined(C89FW_MACOS)
    void* ns_window;     /* NSWindow* */
    void* ns_view;       /* NSView* */
#endif
} C89FW_native_handles_t;

/* Retrieve native handles for external rendering APIs */
C89FW_native_handles_t C89FW_get_native_handles(const C89FW_window_t* window);

/* ========================================================================
   PUBLIC API
   ======================================================================== */

/* --- Window & Input --- */
int  C89FW_open(C89FW_window_t* window, int width, int height, const char* title);
void C89FW_close(C89FW_window_t* window);
int  C89FW_update(C89FW_window_t* window);
int  C89FW_poll_event(C89FW_window_t* window, C89FW_event_t* event);
void C89FW_set_title(C89FW_window_t* window, const char* title);
void C89FW_set_size(C89FW_window_t* window, int width, int height);
void C89FW_apply_resize(C89FW_window_t* window);
double C89FW_get_time(void);

/* --- Software Rendering --- */
void C89FW_set_framebuffer(C89FW_window_t* window, unsigned char* framebuffer, int width, int height);
void C89FW_present(C89FW_window_t* window);

/* --- Polling Helpers (inline) --- */
static int C89FW_key_down(const C89FW_window_t* window, C89FW_key_t key);
static int C89FW_key_pressed(const C89FW_window_t* window, C89FW_key_t key);
static int C89FW_key_released(const C89FW_window_t* window, C89FW_key_t key);
static int C89FW_mouse_down(const C89FW_window_t* window, C89FW_mouse_button_t button);
static int C89FW_mouse_pressed(const C89FW_window_t* window, C89FW_mouse_button_t button);
static int C89FW_mouse_released(const C89FW_window_t* window, C89FW_mouse_button_t button);
static int C89FW_gamepad_present(const C89FW_window_t* window, int gamepad_index);
static int C89FW_gamepad_down(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button);
static int C89FW_gamepad_pressed(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button);
static int C89FW_gamepad_released(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button);
static float C89FW_gamepad_axis(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_axis_t axis);

#ifdef __cplusplus
}
#endif

#endif /* C89FW_H */

/* ================================================================
   IMPLEMENTATION
   ================================================================ */
#ifdef C89FW_IMPLEMENTATION
#ifndef C89FW_IMPLEMENTATION_DONE
#define C89FW_IMPLEMENTATION_DONE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- Event Queue ---------- */
#define C89FW_EVENT_QUEUE_SIZE 256

typedef struct {
    C89FW_event_t events[C89FW_EVENT_QUEUE_SIZE];
    int read_index;
    int write_index;
    int count;
} C89FW_event_queue_t;

static void C89FW_event_queue_init(C89FW_event_queue_t* queue) {
    memset(queue, 0, sizeof(C89FW_event_queue_t));
}

static int C89FW_event_queue_push(C89FW_event_queue_t* queue, const C89FW_event_t* event) {
    if (queue->count >= C89FW_EVENT_QUEUE_SIZE) return 0;
    queue->events[queue->write_index] = *event;
    queue->write_index = (queue->write_index + 1) % C89FW_EVENT_QUEUE_SIZE;
    queue->count++;
    return 1;
}

static int C89FW_event_queue_pop(C89FW_event_queue_t* queue, C89FW_event_t* event) {
    if (queue->count <= 0) return 0;
    *event = queue->events[queue->read_index];
    queue->read_index = (queue->read_index + 1) % C89FW_EVENT_QUEUE_SIZE;
    queue->count--;
    return 1;
}

int C89FW_poll_event(C89FW_window_t* window, C89FW_event_t* event) {
    C89FW_event_queue_t* queue;
    if (!window || !window->event_queue_internal || !event) return 0;
    queue = (C89FW_event_queue_t*)window->event_queue_internal;
    return C89FW_event_queue_pop(queue, event);
}

/* ---------- Polling Helpers ---------- */
static int C89FW_key_down(const C89FW_window_t* window, C89FW_key_t key) {
    return (key >= 0 && key < C89FW_KEY_COUNT) ? window->keys[key] : 0;
}
static int C89FW_key_pressed(const C89FW_window_t* window, C89FW_key_t key) {
    return (key >= 0 && key < C89FW_KEY_COUNT) ? (window->keys[key] && !window->keys_previous[key]) : 0;
}
static int C89FW_key_released(const C89FW_window_t* window, C89FW_key_t key) {
    return (key >= 0 && key < C89FW_KEY_COUNT) ? (!window->keys[key] && window->keys_previous[key]) : 0;
}
static int C89FW_mouse_down(const C89FW_window_t* window, C89FW_mouse_button_t button) {
    return (button >= 0 && button < C89FW_MOUSE_BUTTON_COUNT) ? window->mouse_buttons[button] : 0;
}
static int C89FW_mouse_pressed(const C89FW_window_t* window, C89FW_mouse_button_t button) {
    return (button >= 0 && button < C89FW_MOUSE_BUTTON_COUNT) ? (window->mouse_buttons[button] && !window->mouse_buttons_previous[button]) : 0;
}
static int C89FW_mouse_released(const C89FW_window_t* window, C89FW_mouse_button_t button) {
    return (button >= 0 && button < C89FW_MOUSE_BUTTON_COUNT) ? (!window->mouse_buttons[button] && window->mouse_buttons_previous[button]) : 0;
}
static int C89FW_gamepad_present(const C89FW_window_t* window, int gamepad_index) {
    return (gamepad_index >= 0 && gamepad_index < 4) ? window->gamepads[gamepad_index].present : 0;
}
static int C89FW_gamepad_down(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button) {
    if (gamepad_index >= 0 && gamepad_index < 4 && button >= 0 && button < C89FW_GAMEPAD_BUTTON_COUNT) {
        return window->gamepads[gamepad_index].buttons[button];
    }
    return 0;
}
static int C89FW_gamepad_pressed(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button) {
    if (gamepad_index >= 0 && gamepad_index < 4 && button >= 0 && button < C89FW_GAMEPAD_BUTTON_COUNT) {
        return window->gamepads[gamepad_index].buttons[button] && !window->gamepads[gamepad_index].buttons_previous[button];
    }
    return 0;
}
static int C89FW_gamepad_released(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_button_t button) {
    if (gamepad_index >= 0 && gamepad_index < 4 && button >= 0 && button < C89FW_GAMEPAD_BUTTON_COUNT) {
        return !window->gamepads[gamepad_index].buttons[button] && window->gamepads[gamepad_index].buttons_previous[button];
    }
    return 0;
}
static float C89FW_gamepad_axis(const C89FW_window_t* window, int gamepad_index, C89FW_gamepad_axis_t axis) {
    if (gamepad_index >= 0 && gamepad_index < 4 && axis >= 0 && axis < C89FW_GAMEPAD_AXIS_COUNT) {
        return window->gamepads[gamepad_index].axes[axis];
    }
    return 0.0f;
}

/* ---------- Software Framebuffer ---------- */
void C89FW_set_framebuffer(C89FW_window_t* window, unsigned char* framebuffer, int width, int height) {
    if (!window) return;
    if (window->framebuffer && window->framebuffer_owned) {
        free(window->framebuffer);
    }
    window->framebuffer = framebuffer;
    window->framebuffer_owned = 0;
    window->width = width;
    window->height = height;
}

/* ---------- Cross-platform Event Generation ---------- */
#define C89FW_GAMEPAD_DEADZONE 0.15f

static float C89FW_normalize_gamepad_axis(float raw) {
    if (raw > -C89FW_GAMEPAD_DEADZONE && raw < C89FW_GAMEPAD_DEADZONE) return 0.0f;
    if (raw > 0.0f) return (raw - C89FW_GAMEPAD_DEADZONE) / (1.0f - C89FW_GAMEPAD_DEADZONE);
    return (raw + C89FW_GAMEPAD_DEADZONE) / (1.0f - C89FW_GAMEPAD_DEADZONE);
}

static void C89FW_generate_events(C89FW_window_t* window, double time) {
    C89FW_event_queue_t* queue;
    int i;
    if (!window || !window->event_queue_internal) return;
    queue = (C89FW_event_queue_t*)window->event_queue_internal;

    for (i = 0; i < C89FW_KEY_COUNT; ++i) {
        if (window->keys[i] && !window->keys_previous[i]) {
            C89FW_event_t event; memset(&event, 0, sizeof(event));
            event.type = C89FW_EVENT_KEY_DOWN; event.timestamp = time; event.data.key.code = (C89FW_key_t)i;
            C89FW_event_queue_push(queue, &event);
        } else if (!window->keys[i] && window->keys_previous[i]) {
            C89FW_event_t event; memset(&event, 0, sizeof(event));
            event.type = C89FW_EVENT_KEY_UP; event.timestamp = time; event.data.key.code = (C89FW_key_t)i;
            C89FW_event_queue_push(queue, &event);
        }
    }

    for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++) {
        if (window->mouse_buttons[i] && !window->mouse_buttons_previous[i]) {
            C89FW_event_t event; memset(&event, 0, sizeof(event));
            event.type = C89FW_EVENT_MOUSE_DOWN; event.timestamp = time;
            event.data.mouse_button.button = (C89FW_mouse_button_t)i;
            event.data.mouse_button.x = window->mouse_x; event.data.mouse_button.y = window->mouse_y;
            C89FW_event_queue_push(queue, &event);
        } else if (!window->mouse_buttons[i] && window->mouse_buttons_previous[i]) {
            C89FW_event_t event; memset(&event, 0, sizeof(event));
            event.type = C89FW_EVENT_MOUSE_UP; event.timestamp = time;
            event.data.mouse_button.button = (C89FW_mouse_button_t)i;
            event.data.mouse_button.x = window->mouse_x; event.data.mouse_button.y = window->mouse_y;
            C89FW_event_queue_push(queue, &event);
        }
    }

    if (window->mouse_delta_x != 0 || window->mouse_delta_y != 0) {
        C89FW_event_t event; memset(&event, 0, sizeof(event));
        event.type = C89FW_EVENT_MOUSE_MOVE; event.timestamp = time;
        event.data.mouse_move.x = window->mouse_x; event.data.mouse_move.y = window->mouse_y;
        event.data.mouse_move.delta_x = window->mouse_delta_x; event.data.mouse_move.delta_y = window->mouse_delta_y;
        C89FW_event_queue_push(queue, &event);
    }

    if (window->mouse_scroll_x != 0.0 || window->mouse_scroll_y != 0.0) {
        C89FW_event_t event; memset(&event, 0, sizeof(event));
        event.type = C89FW_EVENT_MOUSE_SCROLL; event.timestamp = time;
        event.data.mouse_scroll.delta_x = window->mouse_scroll_x; event.data.mouse_scroll.delta_y = window->mouse_scroll_y;
        C89FW_event_queue_push(queue, &event);
    }

    if (window->should_close) {
        C89FW_event_t event; memset(&event, 0, sizeof(event));
        event.type = C89FW_EVENT_CLOSE; event.timestamp = time;
        C89FW_event_queue_push(queue, &event);
    }
}

/* ========================================================================
   WINDOWS IMPLEMENTATION
   ======================================================================== */
#if defined(C89FW_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <xinput.h>

typedef struct {
    unsigned char key_changes[C89FW_KEY_COUNT];
    unsigned char mouse_button_changes[C89FW_MOUSE_BUTTON_COUNT];
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    double scroll_delta_x, scroll_delta_y;
    int resized;
    int new_width, new_height;
    int focus_changed;
    int new_focus;
    int pending_resize;
    int pending_width;
    int pending_height;
} C89FW_raw_input_t;

typedef struct {
    HWND hwnd;
    HDC hdc;
    BITMAPINFO bmi;
    HBITMAP backbuffer;
    HDC backbuffer_dc;
    void* backbuffer_bits;
    LARGE_INTEGER frequency;
    LARGE_INTEGER start_time;
    LARGE_INTEGER last_time;
    C89FW_raw_input_t raw;
} C89FW_win32_data_t;

static C89FW_key_t C89FW_win32_translate_key(WPARAM wparam) {
    switch (wparam) {
        case VK_ESCAPE: return C89FW_KEY_ESCAPE;
        case VK_RETURN: return C89FW_KEY_ENTER;
        case VK_SPACE: return C89FW_KEY_SPACE;
        case VK_BACK: return C89FW_KEY_BACKSPACE;
        case VK_TAB: return C89FW_KEY_TAB;
        case VK_SHIFT: return C89FW_KEY_SHIFT;
        case VK_CONTROL: return C89FW_KEY_CONTROL;
        case VK_MENU: return C89FW_KEY_ALT;
        case VK_CAPITAL: return C89FW_KEY_CAPS_LOCK;
        case VK_F1: return C89FW_KEY_F1; case VK_F2: return C89FW_KEY_F2;
        case VK_F3: return C89FW_KEY_F3; case VK_F4: return C89FW_KEY_F4;
        case VK_F5: return C89FW_KEY_F5; case VK_F6: return C89FW_KEY_F6;
        case VK_F7: return C89FW_KEY_F7; case VK_F8: return C89FW_KEY_F8;
        case VK_F9: return C89FW_KEY_F9; case VK_F10: return C89FW_KEY_F10;
        case VK_F11: return C89FW_KEY_F11; case VK_F12: return C89FW_KEY_F12;
        case VK_LEFT: return C89FW_KEY_LEFT; case VK_RIGHT: return C89FW_KEY_RIGHT;
        case VK_UP: return C89FW_KEY_UP; case VK_DOWN: return C89FW_KEY_DOWN;
        case 'A': return C89FW_KEY_A; case 'B': return C89FW_KEY_B;
        case 'C': return C89FW_KEY_C; case 'D': return C89FW_KEY_D;
        case 'E': return C89FW_KEY_E; case 'F': return C89FW_KEY_F;
        case 'G': return C89FW_KEY_G; case 'H': return C89FW_KEY_H;
        case 'I': return C89FW_KEY_I; case 'J': return C89FW_KEY_J;
        case 'K': return C89FW_KEY_K; case 'L': return C89FW_KEY_L;
        case 'M': return C89FW_KEY_M; case 'N': return C89FW_KEY_N;
        case 'O': return C89FW_KEY_O; case 'P': return C89FW_KEY_P;
        case 'Q': return C89FW_KEY_Q; case 'R': return C89FW_KEY_R;
        case 'S': return C89FW_KEY_S; case 'T': return C89FW_KEY_T;
        case 'U': return C89FW_KEY_U; case 'V': return C89FW_KEY_V;
        case 'W': return C89FW_KEY_W; case 'X': return C89FW_KEY_X;
        case 'Y': return C89FW_KEY_Y; case 'Z': return C89FW_KEY_Z;
        case '0': return C89FW_KEY_0; case '1': return C89FW_KEY_1;
        case '2': return C89FW_KEY_2; case '3': return C89FW_KEY_3;
        case '4': return C89FW_KEY_4; case '5': return C89FW_KEY_5;
        case '6': return C89FW_KEY_6; case '7': return C89FW_KEY_7;
        case '8': return C89FW_KEY_8; case '9': return C89FW_KEY_9;
        default: return C89FW_KEY_UNKNOWN;
    }
}

static LRESULT CALLBACK C89FW_win32_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    C89FW_window_t* window = (C89FW_window_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    C89FW_win32_data_t* data;
    C89FW_raw_input_t* raw;
    int i;

    if (!window || !window->internal) return DefWindowProc(hwnd, msg, wparam, lparam);
    data = (C89FW_win32_data_t*)window->internal;
    raw = &data->raw;

    switch (msg) {
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lparam;
            mmi->ptMinTrackSize.x = C89FW_MIN_WIDTH;
            mmi->ptMinTrackSize.y = C89FW_MIN_HEIGHT;
            return 0;
        }
        case WM_CLOSE:
            window->should_close = 1;
            return 0;
        case WM_DESTROY:
            window->should_close = 1;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            RECT rect;
            GetClientRect(hwnd, &rect);
            raw->new_width = rect.right - rect.left;
            raw->new_height = rect.bottom - rect.top;
            raw->resized = 1;
            return 0;
        }
        case WM_SETFOCUS:
            raw->focus_changed = 1;
            raw->new_focus = 1;
            return 0;
        case WM_KILLFOCUS:
            raw->focus_changed = 1;
            raw->new_focus = 0;
            for (i = 0; i < C89FW_KEY_COUNT; i++)
                if (window->keys[i]) raw->key_changes[i] = 2;
            for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++)
                if (window->mouse_buttons[i]) raw->mouse_button_changes[i] = 2;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            C89FW_key_t key = C89FW_win32_translate_key(wparam);
            int was_pressed = (lparam & (1 << 30)) != 0;
            if (key != C89FW_KEY_UNKNOWN && !was_pressed)
                raw->key_changes[key] = 1;
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            C89FW_key_t key = C89FW_win32_translate_key(wparam);
            if (key != C89FW_KEY_UNKNOWN)
                raw->key_changes[key] = 2;
            return 0;
        }
        case WM_MOUSEMOVE:
            raw->mouse_delta_x += GET_X_LPARAM(lparam) - raw->mouse_x;
            raw->mouse_delta_y += GET_Y_LPARAM(lparam) - raw->mouse_y;
            raw->mouse_x = GET_X_LPARAM(lparam);
            raw->mouse_y = GET_Y_LPARAM(lparam);
            return 0;
        case WM_LBUTTONDOWN: raw->mouse_button_changes[C89FW_MOUSE_LEFT] = 1; return 0;
        case WM_LBUTTONUP:   raw->mouse_button_changes[C89FW_MOUSE_LEFT] = 2; return 0;
        case WM_RBUTTONDOWN: raw->mouse_button_changes[C89FW_MOUSE_RIGHT] = 1; return 0;
        case WM_RBUTTONUP:   raw->mouse_button_changes[C89FW_MOUSE_RIGHT] = 2; return 0;
        case WM_MBUTTONDOWN: raw->mouse_button_changes[C89FW_MOUSE_MIDDLE] = 1; return 0;
        case WM_MBUTTONUP:   raw->mouse_button_changes[C89FW_MOUSE_MIDDLE] = 2; return 0;
        case WM_MOUSEWHEEL:
            raw->scroll_delta_y += (double)GET_WHEEL_DELTA_WPARAM(wparam) / (double)WHEEL_DELTA;
            return 0;
        case WM_MOUSEHWHEEL:
            raw->scroll_delta_x += (double)GET_WHEEL_DELTA_WPARAM(wparam) / (double)WHEEL_DELTA;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

double C89FW_get_time(void) {
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    LARGE_INTEGER counter;
    if (!initialized) { QueryPerformanceFrequency(&frequency); initialized = 1; }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

int C89FW_open(C89FW_window_t* window, int width, int height, const char* title) {
    C89FW_win32_data_t* data;
    C89FW_event_queue_t* queue;
    WNDCLASS wc = {0};
    RECT rect;

    if (!window) return 0;
    memset(window, 0, sizeof(C89FW_window_t));

    data = (C89FW_win32_data_t*)malloc(sizeof(C89FW_win32_data_t));
    queue = (C89FW_event_queue_t*)malloc(sizeof(C89FW_event_queue_t));
    if (!data || !queue) { free(data); free(queue); return 0; }
    memset(data, 0, sizeof(C89FW_win32_data_t));
    C89FW_event_queue_init(queue);

    window->internal = data;
    window->event_queue_internal = queue;
    window->width = width;
    window->height = height;
    window->has_focus = 1;

    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = C89FW_win32_window_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "C89FW_WindowClass";

    if (!RegisterClass(&wc)) { free(queue); free(data); return 0; }

    rect.left = 0; rect.top = 0; rect.right = width; rect.bottom = height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    data->hwnd = CreateWindowEx(0, "C89FW_WindowClass", title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!data->hwnd) { free(queue); free(data); return 0; }

    SetWindowLongPtr(data->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    data->hdc = GetDC(data->hwnd);

    data->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    data->bmi.bmiHeader.biWidth = width;
    data->bmi.bmiHeader.biHeight = -height;
    data->bmi.bmiHeader.biPlanes = 1;
    data->bmi.bmiHeader.biBitCount = 32;
    data->bmi.bmiHeader.biCompression = BI_RGB;

    data->backbuffer = CreateDIBSection(data->hdc, &data->bmi, DIB_RGB_COLORS,
                                         &data->backbuffer_bits, NULL, 0);
    data->backbuffer_dc = CreateCompatibleDC(data->hdc);
    if (data->backbuffer) SelectObject(data->backbuffer_dc, data->backbuffer);

    QueryPerformanceFrequency(&data->frequency);
    QueryPerformanceCounter(&data->start_time);
    data->last_time = data->start_time;

    ShowWindow(data->hwnd, SW_SHOW);
    UpdateWindow(data->hwnd);

    return 1;
}

void C89FW_close(C89FW_window_t* window) {
    C89FW_win32_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_win32_data_t*)window->internal;
    if (window->framebuffer && window->framebuffer_owned) {
        free(window->framebuffer);
        window->framebuffer = NULL;
    }
    if (window->event_queue_internal) {
        free(window->event_queue_internal);
        window->event_queue_internal = NULL;
    }
    if (data->backbuffer) DeleteObject(data->backbuffer);
    if (data->backbuffer_dc) DeleteDC(data->backbuffer_dc);
    if (data->hdc) ReleaseDC(data->hwnd, data->hdc);
    if (data->hwnd) DestroyWindow(data->hwnd);
    free(data);
    window->internal = NULL;
}

int C89FW_update(C89FW_window_t* window) {
    C89FW_win32_data_t* data;
    C89FW_raw_input_t* raw;
    MSG msg;
    LARGE_INTEGER current_time;
    int i;

    if (!window || !window->internal) return 0;
    data = (C89FW_win32_data_t*)window->internal;
    raw = &data->raw;
    if (window->should_close) return 0;

    QueryPerformanceCounter(&current_time);
    window->delta_time = (double)(current_time.QuadPart - data->last_time.QuadPart) / (double)data->frequency.QuadPart;
    window->time = (double)(current_time.QuadPart - data->start_time.QuadPart) / (double)data->frequency.QuadPart;
    data->last_time = current_time;

    memcpy(window->keys_previous, window->keys, sizeof(window->keys));
    memcpy(window->mouse_buttons_previous, window->mouse_buttons, sizeof(window->mouse_buttons));
    { int g; for (g = 0; g < 4; g++) memcpy(window->gamepads[g].buttons_previous, window->gamepads[g].buttons, sizeof(window->gamepads[g].buttons)); }

    memset(raw->key_changes, 0, sizeof(raw->key_changes));
    memset(raw->mouse_button_changes, 0, sizeof(raw->mouse_button_changes));
    raw->mouse_delta_x = 0; raw->mouse_delta_y = 0;
    raw->scroll_delta_x = 0.0; raw->scroll_delta_y = 0.0;
    raw->resized = 0; raw->focus_changed = 0;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { window->should_close = 1; return 0; }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    for (i = 0; i < C89FW_KEY_COUNT; i++) {
        if (raw->key_changes[i] == 1) window->keys[i] = 1;
        else if (raw->key_changes[i] == 2) window->keys[i] = 0;
    }
    for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++) {
        if (raw->mouse_button_changes[i] == 1) window->mouse_buttons[i] = 1;
        else if (raw->mouse_button_changes[i] == 2) window->mouse_buttons[i] = 0;
    }

    window->mouse_delta_x = raw->mouse_delta_x;
    window->mouse_delta_y = raw->mouse_delta_y;
    window->mouse_x = raw->mouse_x;
    window->mouse_y = raw->mouse_y;
    window->mouse_scroll_x = raw->scroll_delta_x;
    window->mouse_scroll_y = raw->scroll_delta_y;

    if (raw->resized) {
        C89FW_event_t event;
        raw->pending_resize = 1;
        raw->pending_width = raw->new_width;
        raw->pending_height = raw->new_height;
        memset(&event, 0, sizeof(event));
        event.type = C89FW_EVENT_RESIZE;
        event.timestamp = window->time;
        event.data.resize.width = raw->new_width;
        event.data.resize.height = raw->new_height;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &event);
    }

    if (raw->focus_changed) {
        C89FW_event_t event;
        window->has_focus = raw->new_focus;
        memset(&event, 0, sizeof(event));
        event.type = raw->new_focus ? C89FW_EVENT_FOCUS_GAINED : C89FW_EVENT_FOCUS_LOST;
        event.timestamp = window->time;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &event);
    }

    C89FW_generate_events(window, window->time);

    {
        C89FW_event_queue_t* queue = (C89FW_event_queue_t*)window->event_queue_internal;
        for (i = 0; i < 4; i++) {
            XINPUT_STATE state;
            DWORD result = XInputGetState(i, &state);
            if (result == ERROR_SUCCESS) {
                if (!window->gamepads[i].present) {
                    C89FW_event_t event;
                    memset(&event, 0, sizeof(event));
                    event.type = C89FW_EVENT_GAMEPAD_CONNECT;
                    event.timestamp = window->time;
                    event.data.gamepad_button.gamepad_index = i;
                    C89FW_event_queue_push(queue, &event);
                }
                window->gamepads[i].present = 1;
                {
                    static const struct { WORD flag; C89FW_gamepad_button_t btn; } map[] = {
                        {XINPUT_GAMEPAD_DPAD_UP, C89FW_GAMEPAD_DPAD_UP},
                        {XINPUT_GAMEPAD_DPAD_DOWN, C89FW_GAMEPAD_DPAD_DOWN},
                        {XINPUT_GAMEPAD_DPAD_LEFT, C89FW_GAMEPAD_DPAD_LEFT},
                        {XINPUT_GAMEPAD_DPAD_RIGHT, C89FW_GAMEPAD_DPAD_RIGHT},
                        {XINPUT_GAMEPAD_START, C89FW_GAMEPAD_START},
                        {XINPUT_GAMEPAD_BACK, C89FW_GAMEPAD_BACK},
                        {XINPUT_GAMEPAD_LEFT_THUMB, C89FW_GAMEPAD_LEFT_THUMB},
                        {XINPUT_GAMEPAD_RIGHT_THUMB, C89FW_GAMEPAD_RIGHT_THUMB},
                        {XINPUT_GAMEPAD_LEFT_SHOULDER, C89FW_GAMEPAD_LEFT_BUMPER},
                        {XINPUT_GAMEPAD_RIGHT_SHOULDER, C89FW_GAMEPAD_RIGHT_BUMPER},
                        {XINPUT_GAMEPAD_A, C89FW_GAMEPAD_A},
                        {XINPUT_GAMEPAD_B, C89FW_GAMEPAD_B},
                        {XINPUT_GAMEPAD_X, C89FW_GAMEPAD_X},
                        {XINPUT_GAMEPAD_Y, C89FW_GAMEPAD_Y}
                    };
                    int j;
                    for (j = 0; j < 14; j++) {
                        unsigned char new_state = (state.Gamepad.wButtons & map[j].flag) ? 1 : 0;
                        if (new_state != window->gamepads[i].buttons[map[j].btn]) {
                            C89FW_event_t event;
                            memset(&event, 0, sizeof(event));
                            event.type = new_state ? C89FW_EVENT_GAMEPAD_DOWN : C89FW_EVENT_GAMEPAD_UP;
                            event.timestamp = window->time;
                            event.data.gamepad_button.gamepad_index = i;
                            event.data.gamepad_button.button = map[j].btn;
                            C89FW_event_queue_push(queue, &event);
                        }
                        window->gamepads[i].buttons[map[j].btn] = new_state;
                    }
                }
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_LEFT_X] = C89FW_normalize_gamepad_axis((float)state.Gamepad.sThumbLX / 32767.0f);
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_LEFT_Y] = C89FW_normalize_gamepad_axis((float)state.Gamepad.sThumbLY / 32767.0f);
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_RIGHT_X] = C89FW_normalize_gamepad_axis((float)state.Gamepad.sThumbRX / 32767.0f);
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_RIGHT_Y] = C89FW_normalize_gamepad_axis((float)state.Gamepad.sThumbRY / 32767.0f);
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_LEFT_TRIGGER] = (float)state.Gamepad.bLeftTrigger / 255.0f;
                window->gamepads[i].axes[C89FW_GAMEPAD_AXIS_RIGHT_TRIGGER] = (float)state.Gamepad.bRightTrigger / 255.0f;
            } else if (window->gamepads[i].present) {
                C89FW_event_t event;
                memset(&event, 0, sizeof(event));
                event.type = C89FW_EVENT_GAMEPAD_DISCONNECT;
                event.timestamp = window->time;
                event.data.gamepad_button.gamepad_index = i;
                C89FW_event_queue_push(queue, &event);
                window->gamepads[i].present = 0;
            }
        }
    }

    return 1;
}

void C89FW_present(C89FW_window_t* window) {
    C89FW_win32_data_t* data;
    int use_w, use_h;

    if (!window || !window->internal || !window->framebuffer) return;
    data = (C89FW_win32_data_t*)window->internal;

    if (data->raw.pending_resize) {
        use_w = data->raw.pending_width;
        use_h = data->raw.pending_height;
    } else {
        use_w = window->width;
        use_h = window->height;
    }

    if (!data->backbuffer || data->bmi.bmiHeader.biWidth != use_w ||
        -data->bmi.bmiHeader.biHeight != use_h) {
        if (data->backbuffer) {
            DeleteObject(data->backbuffer);
            DeleteDC(data->backbuffer_dc);
        }

        data->bmi.bmiHeader.biWidth = use_w;
        data->bmi.bmiHeader.biHeight = -use_h;

        data->backbuffer = CreateDIBSection(data->hdc, &data->bmi, DIB_RGB_COLORS,
                                             &data->backbuffer_bits, NULL, 0);
        data->backbuffer_dc = CreateCompatibleDC(data->hdc);
        if (data->backbuffer) SelectObject(data->backbuffer_dc, data->backbuffer);
    }

    if (data->backbuffer_bits && window->framebuffer) {
        memcpy(data->backbuffer_bits, window->framebuffer, use_w * use_h * 4);
        BitBlt(data->hdc, 0, 0, use_w, use_h, data->backbuffer_dc, 0, 0, SRCCOPY);
    }
}

void C89FW_apply_resize(C89FW_window_t* window) {
    C89FW_win32_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_win32_data_t*)window->internal;
    if (data->raw.pending_resize) {
        window->width = data->raw.pending_width;
        window->height = data->raw.pending_height;
        data->raw.pending_resize = 0;
    }
}

void C89FW_set_title(C89FW_window_t* window, const char* title) {
    C89FW_win32_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_win32_data_t*)window->internal;
    SetWindowText(data->hwnd, title);
}

void C89FW_set_size(C89FW_window_t* window, int width, int height) {
    C89FW_win32_data_t* data;
    RECT rect;
    if (!window || !window->internal) return;
    data = (C89FW_win32_data_t*)window->internal;
    rect.left = 0; rect.top = 0; rect.right = width; rect.bottom = height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(data->hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}

C89FW_native_handles_t C89FW_get_native_handles(const C89FW_window_t* window) {
    C89FW_native_handles_t handles = {0};
    if (window && window->internal) {
        C89FW_win32_data_t* data = (C89FW_win32_data_t*)window->internal;
        handles.hwnd = data->hwnd;
    }
    return handles;
}

/* ========================================================================
   LINUX / X11 IMPLEMENTATION
   ======================================================================== */
#elif defined(C89FW_LINUX)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

typedef struct {
    unsigned char key_changes[C89FW_KEY_COUNT];
    unsigned char mouse_button_changes[C89FW_MOUSE_BUTTON_COUNT];
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    double scroll_delta_x, scroll_delta_y;
    int resized;
    int new_width, new_height;
    int focus_changed;
    int new_focus;
} C89FW_raw_input_t;

typedef struct {
    Display* display;
    Window window;
    GC gc;
    XImage* ximage;
    Atom wm_delete_message;
    struct timeval start_time;
    struct timeval last_time;
    int screen;
    C89FW_raw_input_t raw;
    int pending_resize;
    int pending_width;
    int pending_height;
    int use_shm;
    XShmSegmentInfo shminfo;
} C89FW_x11_data_t;

static C89FW_key_t C89FW_x11_translate_key(KeySym keysym) {
    switch (keysym) {
        case XK_Escape: return C89FW_KEY_ESCAPE;
        case XK_Return: return C89FW_KEY_ENTER;
        case XK_space: return C89FW_KEY_SPACE;
        case XK_BackSpace: return C89FW_KEY_BACKSPACE;
        case XK_Tab: return C89FW_KEY_TAB;
        case XK_Shift_L: case XK_Shift_R: return C89FW_KEY_SHIFT;
        case XK_Control_L: case XK_Control_R: return C89FW_KEY_CONTROL;
        case XK_Alt_L: case XK_Alt_R: return C89FW_KEY_ALT;
        case XK_Caps_Lock: return C89FW_KEY_CAPS_LOCK;
        case XK_F1: return C89FW_KEY_F1; case XK_F2: return C89FW_KEY_F2;
        case XK_F3: return C89FW_KEY_F3; case XK_F4: return C89FW_KEY_F4;
        case XK_F5: return C89FW_KEY_F5; case XK_F6: return C89FW_KEY_F6;
        case XK_F7: return C89FW_KEY_F7; case XK_F8: return C89FW_KEY_F8;
        case XK_F9: return C89FW_KEY_F9; case XK_F10: return C89FW_KEY_F10;
        case XK_F11: return C89FW_KEY_F11; case XK_F12: return C89FW_KEY_F12;
        case XK_Left: return C89FW_KEY_LEFT; case XK_Right: return C89FW_KEY_RIGHT;
        case XK_Up: return C89FW_KEY_UP; case XK_Down: return C89FW_KEY_DOWN;
        case XK_a: case XK_A: return C89FW_KEY_A; case XK_b: case XK_B: return C89FW_KEY_B;
        case XK_c: case XK_C: return C89FW_KEY_C; case XK_d: case XK_D: return C89FW_KEY_D;
        case XK_e: case XK_E: return C89FW_KEY_E; case XK_f: case XK_F: return C89FW_KEY_F;
        case XK_g: case XK_G: return C89FW_KEY_G; case XK_h: case XK_H: return C89FW_KEY_H;
        case XK_i: case XK_I: return C89FW_KEY_I; case XK_j: case XK_J: return C89FW_KEY_J;
        case XK_k: case XK_K: return C89FW_KEY_K; case XK_l: case XK_L: return C89FW_KEY_L;
        case XK_m: case XK_M: return C89FW_KEY_M; case XK_n: case XK_N: return C89FW_KEY_N;
        case XK_o: case XK_O: return C89FW_KEY_O; case XK_p: case XK_P: return C89FW_KEY_P;
        case XK_q: case XK_Q: return C89FW_KEY_Q; case XK_r: case XK_R: return C89FW_KEY_R;
        case XK_s: case XK_S: return C89FW_KEY_S; case XK_t: case XK_T: return C89FW_KEY_T;
        case XK_u: case XK_U: return C89FW_KEY_U; case XK_v: case XK_V: return C89FW_KEY_V;
        case XK_w: case XK_W: return C89FW_KEY_W; case XK_x: case XK_X: return C89FW_KEY_X;
        case XK_y: case XK_Y: return C89FW_KEY_Y; case XK_z: case XK_Z: return C89FW_KEY_Z;
        case XK_0: return C89FW_KEY_0; case XK_1: return C89FW_KEY_1;
        case XK_2: return C89FW_KEY_2; case XK_3: return C89FW_KEY_3;
        case XK_4: return C89FW_KEY_4; case XK_5: return C89FW_KEY_5;
        case XK_6: return C89FW_KEY_6; case XK_7: return C89FW_KEY_7;
        case XK_8: return C89FW_KEY_8; case XK_9: return C89FW_KEY_9;
        default: return C89FW_KEY_UNKNOWN;
    }
}

static int C89FW_x11_init_shm(C89FW_x11_data_t* data) {
    int major, minor;
    Bool pixmaps;
    if (!XShmQueryVersion(data->display, &major, &minor, &pixmaps)) return 0;
    return 1;
}

static XImage* C89FW_x11_create_shm_image(C89FW_x11_data_t* data, int width, int height) {
    XImage* img;
    img = XShmCreateImage(data->display,
                          DefaultVisual(data->display, data->screen),
                          DefaultDepth(data->display, data->screen),
                          ZPixmap, NULL, &data->shminfo, width, height);
    if (!img) return NULL;
    data->shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    if (data->shminfo.shmid < 0) { XDestroyImage(img); return NULL; }
    data->shminfo.shmaddr = img->data = (char*)shmat(data->shminfo.shmid, 0, 0);
    if (data->shminfo.shmaddr == (char*)-1) { XDestroyImage(img); shmctl(data->shminfo.shmid, IPC_RMID, 0); return NULL; }
    data->shminfo.readOnly = False;
    if (!XShmAttach(data->display, &data->shminfo)) {
        XDestroyImage(img);
        shmdt(data->shminfo.shmaddr);
        shmctl(data->shminfo.shmid, IPC_RMID, 0);
        return NULL;
    }
    XSync(data->display, False);
    return img;
}

static void C89FW_x11_destroy_shm_image(C89FW_x11_data_t* data) {
    if (data->ximage) {
        XShmDetach(data->display, &data->shminfo);
        XDestroyImage(data->ximage);
        data->ximage = NULL;
        shmdt(data->shminfo.shmaddr);
        shmctl(data->shminfo.shmid, IPC_RMID, 0);
    }
}

double C89FW_get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

int C89FW_open(C89FW_window_t* window, int width, int height, const char* title) {
    C89FW_x11_data_t* data;
    C89FW_event_queue_t* queue;
    XSetWindowAttributes swa;
    XSizeHints hints;

    if (!window) return 0;
    memset(window, 0, sizeof(C89FW_window_t));

    data = (C89FW_x11_data_t*)malloc(sizeof(C89FW_x11_data_t));
    queue = (C89FW_event_queue_t*)malloc(sizeof(C89FW_event_queue_t));
    if (!data || !queue) { free(data); free(queue); return 0; }
    memset(data, 0, sizeof(C89FW_x11_data_t));
    C89FW_event_queue_init(queue);

    window->internal = data;
    window->event_queue_internal = queue;
    window->width = width;
    window->height = height;
    window->has_focus = 1;

    XInitThreads();
    data->display = XOpenDisplay(NULL);
    if (!data->display) { free(queue); free(data); return 0; }

    data->screen = DefaultScreen(data->display);
    data->use_shm = C89FW_x11_init_shm(data);

    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask;

    data->window = XCreateWindow(data->display, RootWindow(data->display, data->screen),
        0, 0, width, height, 0, DefaultDepth(data->display, data->screen),
        InputOutput, DefaultVisual(data->display, data->screen), CWEventMask, &swa);

    if (!data->window) { XCloseDisplay(data->display); free(queue); free(data); return 0; }

    XStoreName(data->display, data->window, title);

    hints.flags = PSize | PMinSize;
    hints.width = width;
    hints.height = height;
    hints.min_width = C89FW_MIN_WIDTH;
    hints.min_height = C89FW_MIN_HEIGHT;
    XSetNormalHints(data->display, data->window, &hints);

    data->wm_delete_message = XInternAtom(data->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(data->display, data->window, &data->wm_delete_message, 1);

    data->gc = XCreateGC(data->display, data->window, 0, NULL);
    data->ximage = NULL;

    XMapWindow(data->display, data->window);
    XFlush(data->display);

    gettimeofday(&data->start_time, NULL);
    data->last_time = data->start_time;

    return 1;
}

void C89FW_close(C89FW_window_t* window) {
    C89FW_x11_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_x11_data_t*)window->internal;
    if (window->framebuffer && window->framebuffer_owned) {
        free(window->framebuffer);
        window->framebuffer = NULL;
    }
    if (window->event_queue_internal) {
        free(window->event_queue_internal);
        window->event_queue_internal = NULL;
    }
    if (data->ximage) {
        if (data->use_shm) C89FW_x11_destroy_shm_image(data);
        else { data->ximage->data = NULL; XDestroyImage(data->ximage); }
    }
    if (data->gc) XFreeGC(data->display, data->gc);
    if (data->window) XDestroyWindow(data->display, data->window);
    if (data->display) XCloseDisplay(data->display);
    free(data);
    window->internal = NULL;
}

int C89FW_update(C89FW_window_t* window) {
    C89FW_x11_data_t* data;
    C89FW_raw_input_t* raw;
    XEvent xevent;
    struct timeval current_time;
    int i;

    if (!window || !window->internal) return 0;
    data = (C89FW_x11_data_t*)window->internal;
    raw = &data->raw;
    if (window->should_close) return 0;

    gettimeofday(&current_time, NULL);
    window->delta_time = (double)(current_time.tv_sec - data->last_time.tv_sec) +
                         (double)(current_time.tv_usec - data->last_time.tv_usec) / 1000000.0;
    window->time = (double)(current_time.tv_sec - data->start_time.tv_sec) +
                   (double)(current_time.tv_usec - data->start_time.tv_usec) / 1000000.0;
    data->last_time = current_time;

    memcpy(window->keys_previous, window->keys, sizeof(window->keys));
    memcpy(window->mouse_buttons_previous, window->mouse_buttons, sizeof(window->mouse_buttons));
    { int g; for (g = 0; g < 4; g++) memcpy(window->gamepads[g].buttons_previous, window->gamepads[g].buttons, sizeof(window->gamepads[g].buttons)); }

    memset(raw->key_changes, 0, sizeof(raw->key_changes));
    memset(raw->mouse_button_changes, 0, sizeof(raw->mouse_button_changes));
    raw->mouse_delta_x = 0; raw->mouse_delta_y = 0;
    raw->scroll_delta_x = 0.0; raw->scroll_delta_y = 0.0;
    raw->resized = 0; raw->focus_changed = 0;

    while (XPending(data->display)) {
        XNextEvent(data->display, &xevent);

        switch (xevent.type) {
            case ClientMessage:
                if ((Atom)xevent.xclient.data.l[0] == data->wm_delete_message)
                    window->should_close = 1;
                break;
            case KeyPress: {
                KeySym keysym = XLookupKeysym(&xevent.xkey, 0);
                C89FW_key_t key = C89FW_x11_translate_key(keysym);
                if (key != C89FW_KEY_UNKNOWN) raw->key_changes[key] = 1;
                break;
            }
            case KeyRelease: {
                KeySym keysym = XLookupKeysym(&xevent.xkey, 0);
                C89FW_key_t key = C89FW_x11_translate_key(keysym);
                if (XEventsQueued(data->display, QueuedAfterReading)) {
                    XEvent next;
                    XPeekEvent(data->display, &next);
                    if (next.type == KeyPress && next.xkey.time == xevent.xkey.time &&
                        next.xkey.keycode == xevent.xkey.keycode) {
                        XNextEvent(data->display, &xevent);
                        break;
                    }
                }
                if (key != C89FW_KEY_UNKNOWN) raw->key_changes[key] = 2;
                break;
            }
            case MotionNotify:
                raw->mouse_delta_x += xevent.xmotion.x - raw->mouse_x;
                raw->mouse_delta_y += xevent.xmotion.y - raw->mouse_y;
                raw->mouse_x = xevent.xmotion.x;
                raw->mouse_y = xevent.xmotion.y;
                break;
            case ButtonPress:
                raw->mouse_x = xevent.xbutton.x;
                raw->mouse_y = xevent.xbutton.y;
                switch (xevent.xbutton.button) {
                    case Button1: raw->mouse_button_changes[C89FW_MOUSE_LEFT] = 1; break;
                    case Button2: raw->mouse_button_changes[C89FW_MOUSE_MIDDLE] = 1; break;
                    case Button3: raw->mouse_button_changes[C89FW_MOUSE_RIGHT] = 1; break;
                    case Button4: raw->scroll_delta_y += 1.0; break;
                    case Button5: raw->scroll_delta_y -= 1.0; break;
                }
                break;
            case ButtonRelease:
                raw->mouse_x = xevent.xbutton.x;
                raw->mouse_y = xevent.xbutton.y;
                switch (xevent.xbutton.button) {
                    case Button1: raw->mouse_button_changes[C89FW_MOUSE_LEFT] = 2; break;
                    case Button2: raw->mouse_button_changes[C89FW_MOUSE_MIDDLE] = 2; break;
                    case Button3: raw->mouse_button_changes[C89FW_MOUSE_RIGHT] = 2; break;
                }
                break;
            case ConfigureNotify:
                if (xevent.xconfigure.width != window->width || xevent.xconfigure.height != window->height) {
                    raw->new_width = xevent.xconfigure.width;
                    raw->new_height = xevent.xconfigure.height;
                    raw->resized = 1;
                }
                break;
            case FocusIn:
                raw->focus_changed = 1;
                raw->new_focus = 1;
                break;
            case FocusOut: {
                int j;
                raw->focus_changed = 1;
                raw->new_focus = 0;
                for (j = 0; j < C89FW_KEY_COUNT; j++)
                    if (window->keys[j]) raw->key_changes[j] = 2;
                for (j = 0; j < C89FW_MOUSE_BUTTON_COUNT; j++)
                    if (window->mouse_buttons[j]) raw->mouse_button_changes[j] = 2;
                break;
            }
        }
    }

    for (i = 0; i < C89FW_KEY_COUNT; i++) {
        if (raw->key_changes[i] == 1) window->keys[i] = 1;
        else if (raw->key_changes[i] == 2) window->keys[i] = 0;
    }
    for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++) {
        if (raw->mouse_button_changes[i] == 1) window->mouse_buttons[i] = 1;
        else if (raw->mouse_button_changes[i] == 2) window->mouse_buttons[i] = 0;
    }

    window->mouse_delta_x = raw->mouse_delta_x;
    window->mouse_delta_y = raw->mouse_delta_y;
    window->mouse_x = raw->mouse_x;
    window->mouse_y = raw->mouse_y;
    window->mouse_scroll_x = raw->scroll_delta_x;
    window->mouse_scroll_y = raw->scroll_delta_y;

    if (raw->resized) {
        C89FW_event_t event;
        data->pending_resize = 1;
        data->pending_width = raw->new_width;
        data->pending_height = raw->new_height;
        memset(&event, 0, sizeof(event));
        event.type = C89FW_EVENT_RESIZE;
        event.timestamp = window->time;
        event.data.resize.width = raw->new_width;
        event.data.resize.height = raw->new_height;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &event);
    }

    if (raw->focus_changed) {
        C89FW_event_t event;
        window->has_focus = raw->new_focus;
        memset(&event, 0, sizeof(event));
        event.type = raw->new_focus ? C89FW_EVENT_FOCUS_GAINED : C89FW_EVENT_FOCUS_LOST;
        event.timestamp = window->time;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &event);
    }

    C89FW_generate_events(window, window->time);

    for (i = 0; i < 4; i++) window->gamepads[i].present = 0;

    return 1;
}

void C89FW_apply_resize(C89FW_window_t* window) {
    C89FW_x11_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_x11_data_t*)window->internal;
    if (data->pending_resize) {
        window->width = data->pending_width;
        window->height = data->pending_height;
        data->pending_resize = 0;
    }
}

void C89FW_present(C89FW_window_t* window) {
    C89FW_x11_data_t* data;
    int present_w, present_h;
    unsigned char* fb;

    if (!window || !window->internal || !window->framebuffer) return;
    data = (C89FW_x11_data_t*)window->internal;

    if (data->pending_resize) return;

    present_w = window->width;
    present_h = window->height;
    fb = window->framebuffer;

    if (data->use_shm) {
        if (!data->ximage ||
            data->ximage->width != present_w ||
            data->ximage->height != present_h) {
            if (data->ximage) C89FW_x11_destroy_shm_image(data);
            data->ximage = C89FW_x11_create_shm_image(data, present_w, present_h);
        }

        if (data->ximage && data->ximage->data) {
            memcpy(data->ximage->data, fb, present_w * present_h * 4);
            XShmPutImage(data->display, data->window, data->gc, data->ximage,
                        0, 0, 0, 0, present_w, present_h, False);
            XFlush(data->display);
        }
    } else {
        if (!data->ximage ||
            data->ximage->width != present_w ||
            data->ximage->height != present_h) {
            if (data->ximage) {
                data->ximage->data = NULL;
                XDestroyImage(data->ximage);
            }
            data->ximage = XCreateImage(data->display,
                DefaultVisual(data->display, data->screen),
                DefaultDepth(data->display, data->screen),
                ZPixmap, 0,
                (char*)fb,
                present_w, present_h, 32, 0);
        }

        if (!data->ximage) return;
        data->ximage->data = (char*)fb;
        XPutImage(data->display, data->window, data->gc, data->ximage,
            0, 0, 0, 0, present_w, present_h);
        XFlush(data->display);
    }
}

void C89FW_set_title(C89FW_window_t* window, const char* title) {
    C89FW_x11_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_x11_data_t*)window->internal;
    XStoreName(data->display, data->window, title);
    XFlush(data->display);
}

void C89FW_set_size(C89FW_window_t* window, int width, int height) {
    C89FW_x11_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_x11_data_t*)window->internal;
    XResizeWindow(data->display, data->window, width, height);
    XFlush(data->display);
}

C89FW_native_handles_t C89FW_get_native_handles(const C89FW_window_t* window) {
    C89FW_native_handles_t handles = {0};
    if (window && window->internal) {
        C89FW_x11_data_t* data = (C89FW_x11_data_t*)window->internal;
        handles.display = data->display;
        handles.window = data->window;
    }
    return handles;
}

/* ========================================================================
   MACOS / COCOA IMPLEMENTATION
   ======================================================================== */
#elif defined(C89FW_MACOS)

#include <Cocoa/Cocoa.h>
#include <mach/mach_time.h>
#include <sys/time.h>
#include <QuartzCore/QuartzCore.h>

typedef struct {
    unsigned char key_changes[C89FW_KEY_COUNT];
    unsigned char mouse_button_changes[C89FW_MOUSE_BUTTON_COUNT];
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    double scroll_delta_x, scroll_delta_y;
    int resized;
    int new_width, new_height;
    int focus_changed;
    int new_focus;
} C89FW_raw_input_t;

typedef struct {
    NSWindow* ns_window;
    NSView* ns_view;
    NSAutoreleasePool* pool;
    CALayer* contentLayer;
    uint64_t start_time;
    uint64_t last_time;
    mach_timebase_info_data_t timebase;
    C89FW_raw_input_t raw;
    int pending_resize;
    int pending_width;
    int pending_height;
} C89FW_mac_data_t;

@interface C89FW_WindowDelegate : NSObject <NSWindowDelegate>
{
    C89FW_window_t* window;
}
- (id)initWithWindow:(C89FW_window_t*)w;
@end

@implementation C89FW_WindowDelegate
- (id)initWithWindow:(C89FW_window_t*)w
{
    self = [super init];
    if (self) window = w;
    return self;
}

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize
{
    (void)sender;
    if (frameSize.width < (CGFloat)C89FW_MIN_WIDTH)
        frameSize.width = (CGFloat)C89FW_MIN_WIDTH;
    if (frameSize.height < (CGFloat)C89FW_MIN_HEIGHT)
        frameSize.height = (CGFloat)C89FW_MIN_HEIGHT;
    return frameSize;
}

- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    if (window) window->should_close = 1;
    return YES;
}

- (void)windowDidResize:(NSNotification *)notification
{
    if (window) {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        NSRect frame = [[notification object] frame];
        data->raw.new_width = (int)frame.size.width;
        data->raw.new_height = (int)frame.size.height;
        data->raw.resized = 1;
    }
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;
    if (window) {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        data->raw.focus_changed = 1;
        data->raw.new_focus = 1;
    }
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    if (window) {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        int i;
        data->raw.focus_changed = 1;
        data->raw.new_focus = 0;
        for (i = 0; i < C89FW_KEY_COUNT; i++)
            if (window->keys[i]) data->raw.key_changes[i] = 2;
        for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++)
            if (window->mouse_buttons[i]) data->raw.mouse_button_changes[i] = 2;
    }
}

- (void)dealloc { [super dealloc]; }
@end

@interface C89FW_WindowView : NSView
{
    C89FW_window_t* window;
    NSTrackingArea* trackingArea;
}
- (id)initWithFrame:(NSRect)frame window:(C89FW_window_t*)w;
- (C89FW_key_t)translateKey:(unsigned short)keyCode;
@end

@implementation C89FW_WindowView

- (id)initWithFrame:(NSRect)frame window:(C89FW_window_t*)w
{
    self = [super initWithFrame:frame];
    if (self) {
        window = w;
        [self setWantsLayer:YES];
        trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
            options:(NSTrackingMouseMoved | NSTrackingActiveAlways)
            owner:self userInfo:nil];
        [self addTrackingArea:trackingArea];
    }
    return self;
}

- (C89FW_key_t)translateKey:(unsigned short)keyCode
{
    switch (keyCode) {
        case 53: return C89FW_KEY_ESCAPE;
        case 36: return C89FW_KEY_ENTER;
        case 49: return C89FW_KEY_SPACE;
        case 51: return C89FW_KEY_BACKSPACE;
        case 48: return C89FW_KEY_TAB;
        case 56: return C89FW_KEY_SHIFT;
        case 59: return C89FW_KEY_CONTROL;
        case 55: return C89FW_KEY_ALT;
        case 57: return C89FW_KEY_CAPS_LOCK;
        case 122: return C89FW_KEY_F1; case 120: return C89FW_KEY_F2;
        case 99: return C89FW_KEY_F3; case 118: return C89FW_KEY_F4;
        case 96: return C89FW_KEY_F5; case 97: return C89FW_KEY_F6;
        case 98: return C89FW_KEY_F7; case 100: return C89FW_KEY_F8;
        case 101: return C89FW_KEY_F9; case 109: return C89FW_KEY_F10;
        case 103: return C89FW_KEY_F11; case 111: return C89FW_KEY_F12;
        case 123: return C89FW_KEY_LEFT; case 124: return C89FW_KEY_RIGHT;
        case 125: return C89FW_KEY_DOWN; case 126: return C89FW_KEY_UP;
        case 0: return C89FW_KEY_A; case 11: return C89FW_KEY_B;
        case 8: return C89FW_KEY_C; case 2: return C89FW_KEY_D;
        case 14: return C89FW_KEY_E; case 3: return C89FW_KEY_F;
        case 5: return C89FW_KEY_G; case 4: return C89FW_KEY_H;
        case 34: return C89FW_KEY_I; case 38: return C89FW_KEY_J;
        case 40: return C89FW_KEY_K; case 37: return C89FW_KEY_L;
        case 46: return C89FW_KEY_M; case 45: return C89FW_KEY_N;
        case 31: return C89FW_KEY_O; case 35: return C89FW_KEY_P;
        case 12: return C89FW_KEY_Q; case 15: return C89FW_KEY_R;
        case 1: return C89FW_KEY_S; case 17: return C89FW_KEY_T;
        case 32: return C89FW_KEY_U; case 9: return C89FW_KEY_V;
        case 13: return C89FW_KEY_W; case 7: return C89FW_KEY_X;
        case 16: return C89FW_KEY_Y; case 6: return C89FW_KEY_Z;
        case 29: return C89FW_KEY_0; case 18: return C89FW_KEY_1;
        case 19: return C89FW_KEY_2; case 20: return C89FW_KEY_3;
        case 21: return C89FW_KEY_4; case 23: return C89FW_KEY_5;
        case 22: return C89FW_KEY_6; case 26: return C89FW_KEY_7;
        case 28: return C89FW_KEY_8; case 25: return C89FW_KEY_9;
        default: return C89FW_KEY_UNKNOWN;
    }
}

- (void)mouseMoved:(NSEvent *)event
{
    if (!window) return;
    {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        NSPoint point = [event locationInWindow];
        int new_x = (int)point.x;
        int new_y = (int)([self frame].size.height - point.y);
        data->raw.mouse_delta_x += new_x - data->raw.mouse_x;
        data->raw.mouse_delta_y += new_y - data->raw.mouse_y;
        data->raw.mouse_x = new_x;
        data->raw.mouse_y = new_y;
    }
}

- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }

- (void)mouseDown:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_LEFT] = 1; }
- (void)mouseUp:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_LEFT] = 2; }
- (void)rightMouseDown:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_RIGHT] = 1; }
- (void)rightMouseUp:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_RIGHT] = 2; }
- (void)otherMouseDown:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_MIDDLE] = 1; }
- (void)otherMouseUp:(NSEvent *)event
    { (void)event; if (window) ((C89FW_mac_data_t*)window->internal)->raw.mouse_button_changes[C89FW_MOUSE_MIDDLE] = 2; }

- (void)scrollWheel:(NSEvent *)event
{
    if (window) {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        data->raw.scroll_delta_x += [event scrollingDeltaX];
        data->raw.scroll_delta_y += [event scrollingDeltaY];
    }
}

- (void)keyDown:(NSEvent *)event
{
    if (!window) return;
    if (![event isARepeat]) {
        C89FW_key_t key = [self translateKey:[event keyCode]];
        if (key != C89FW_KEY_UNKNOWN)
            ((C89FW_mac_data_t*)window->internal)->raw.key_changes[key] = 1;
    }
}

- (void)keyUp:(NSEvent *)event
{
    if (!window) return;
    {
        C89FW_key_t key = [self translateKey:[event keyCode]];
        if (key != C89FW_KEY_UNKNOWN)
            ((C89FW_mac_data_t*)window->internal)->raw.key_changes[key] = 2;
    }
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    if (trackingArea) { [self removeTrackingArea:trackingArea]; [trackingArea release]; }
    [super dealloc];
}
@end

double C89FW_get_time(void) {
    static mach_timebase_info_data_t timebase;
    static int initialized = 0;
    uint64_t time;
    if (!initialized) { mach_timebase_info(&timebase); initialized = 1; }
    time = mach_absolute_time();
    return (double)time * (double)timebase.numer / (double)timebase.denom / 1000000000.0;
}

int C89FW_open(C89FW_window_t* window, int width, int height, const char* title) {
    C89FW_mac_data_t* data;
    C89FW_event_queue_t* queue;
    NSRect frame;
    C89FW_WindowDelegate* delegate;
    C89FW_WindowView* view;
    NSString* titleString;

    if (!window) return 0;
    memset(window, 0, sizeof(C89FW_window_t));

    data = (C89FW_mac_data_t*)malloc(sizeof(C89FW_mac_data_t));
    queue = (C89FW_event_queue_t*)malloc(sizeof(C89FW_event_queue_t));
    if (!data || !queue) { free(data); free(queue); return 0; }
    memset(data, 0, sizeof(C89FW_mac_data_t));
    C89FW_event_queue_init(queue);

    window->internal = data;
    window->event_queue_internal = queue;
    window->width = width;
    window->height = height;
    window->has_focus = 1;

    data->pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:0];

    frame = NSMakeRect(0.0f, 0.0f, (CGFloat)width, (CGFloat)height);

    data->ns_window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                   NSMiniaturizableWindowMask | NSResizableWindowMask)
        backing:NSBackingStoreBuffered defer:NO];

    titleString = [NSString stringWithUTF8String:title];
    [data->ns_window setTitle:titleString];
    [data->ns_window setAcceptsMouseMovedEvents:YES];

    delegate = [[C89FW_WindowDelegate alloc] initWithWindow:window];
    [data->ns_window setDelegate:delegate];

    view = [[C89FW_WindowView alloc] initWithFrame:frame window:window];

    data->contentLayer = [CALayer layer];
    [view setLayer:data->contentLayer];
    [view setWantsLayer:YES];

    [data->ns_window setContentView:view];
    [data->ns_window makeFirstResponder:view];
    [data->ns_window makeKeyAndOrderFront:nil];

    [NSApp activateIgnoringOtherApps:YES];

    data->ns_view = view;

    mach_timebase_info(&data->timebase);
    data->start_time = mach_absolute_time();
    data->last_time = data->start_time;

    return 1;
}

void C89FW_close(C89FW_window_t* window) {
    C89FW_mac_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_mac_data_t*)window->internal;
    if (window->framebuffer && window->framebuffer_owned) {
        free(window->framebuffer);
        window->framebuffer = NULL;
    }
    if (window->event_queue_internal) {
        free(window->event_queue_internal);
        window->event_queue_internal = NULL;
    }
    if (data->ns_window) { [data->ns_window close]; [data->ns_window release]; }
    if (data->pool) [data->pool release];
    free(data);
    window->internal = NULL;
}

int C89FW_update(C89FW_window_t* window) {
    C89FW_mac_data_t* data;
    C89FW_raw_input_t* raw;
    NSEvent* event;
    uint64_t current_time;
    int i;

    if (!window || !window->internal) return 0;
    data = (C89FW_mac_data_t*)window->internal;
    raw = &data->raw;
    if (window->should_close) return 0;

    current_time = mach_absolute_time();
    window->delta_time = (double)(current_time - data->last_time) *
                         (double)data->timebase.numer / (double)data->timebase.denom / 1000000000.0;
    window->time = (double)(current_time - data->start_time) *
                   (double)data->timebase.numer / (double)data->timebase.denom / 1000000000.0;
    data->last_time = current_time;

    memcpy(window->keys_previous, window->keys, sizeof(window->keys));
    memcpy(window->mouse_buttons_previous, window->mouse_buttons, sizeof(window->mouse_buttons));
    { int g; for (g = 0; g < 4; g++) memcpy(window->gamepads[g].buttons_previous, window->gamepads[g].buttons, sizeof(window->gamepads[g].buttons)); }

    memset(raw->key_changes, 0, sizeof(raw->key_changes));
    memset(raw->mouse_button_changes, 0, sizeof(raw->mouse_button_changes));
    raw->mouse_delta_x = 0; raw->mouse_delta_y = 0;
    raw->scroll_delta_x = 0.0; raw->scroll_delta_y = 0.0;
    raw->resized = 0; raw->focus_changed = 0;

    event = [NSApp nextEventMatchingMask:NSAnyEventMask
                               untilDate:[NSDate distantPast]
                                  inMode:NSDefaultRunLoopMode
                                 dequeue:YES];
    while (event) {
        [NSApp sendEvent:event];
        event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
    }

    for (i = 0; i < C89FW_KEY_COUNT; i++) {
        if (raw->key_changes[i] == 1) window->keys[i] = 1;
        else if (raw->key_changes[i] == 2) window->keys[i] = 0;
    }
    for (i = 0; i < C89FW_MOUSE_BUTTON_COUNT; i++) {
        if (raw->mouse_button_changes[i] == 1) window->mouse_buttons[i] = 1;
        else if (raw->mouse_button_changes[i] == 2) window->mouse_buttons[i] = 0;
    }

    window->mouse_delta_x = raw->mouse_delta_x;
    window->mouse_delta_y = raw->mouse_delta_y;
    window->mouse_x = raw->mouse_x;
    window->mouse_y = raw->mouse_y;
    window->mouse_scroll_x = raw->scroll_delta_x;
    window->mouse_scroll_y = raw->scroll_delta_y;

    if (raw->resized) {
        C89FW_event_t wevent;
        data->pending_resize = 1;
        data->pending_width = raw->new_width;
        data->pending_height = raw->new_height;
        memset(&wevent, 0, sizeof(wevent));
        wevent.type = C89FW_EVENT_RESIZE;
        wevent.timestamp = window->time;
        wevent.data.resize.width = raw->new_width;
        wevent.data.resize.height = raw->new_height;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &wevent);
    }

    if (raw->focus_changed) {
        C89FW_event_t wevent;
        window->has_focus = raw->new_focus;
        memset(&wevent, 0, sizeof(wevent));
        wevent.type = raw->new_focus ? C89FW_EVENT_FOCUS_GAINED : C89FW_EVENT_FOCUS_LOST;
        wevent.timestamp = window->time;
        C89FW_event_queue_push((C89FW_event_queue_t*)window->event_queue_internal, &wevent);
    }

    C89FW_generate_events(window, window->time);

    for (i = 0; i < 4; i++) window->gamepads[i].present = 0;

    return 1;
}

void C89FW_apply_resize(C89FW_window_t* window) {
    C89FW_mac_data_t* data;
    if (!window || !window->internal) return;
    data = (C89FW_mac_data_t*)window->internal;
    if (data->pending_resize) {
        window->width = data->pending_width;
        window->height = data->pending_height;
        data->pending_resize = 0;
    }
}

void C89FW_present(C89FW_window_t* window) {
    C89FW_mac_data_t* data;
    CGColorSpaceRef colorSpace;
    CGDataProviderRef provider;
    CGImageRef image;
    int use_w, use_h;

    if (!window || !window->internal || !window->framebuffer) return;
    data = (C89FW_mac_data_t*)window->internal;

    if (data->pending_resize) {
        use_w = data->pending_width;
        use_h = data->pending_height;
    } else {
        use_w = window->width;
        use_h = window->height;
    }

    if (!data->contentLayer) return;

    colorSpace = CGColorSpaceCreateDeviceRGB();
    provider = CGDataProviderCreateWithData(NULL, window->framebuffer,
        use_w * use_h * 4, NULL);

    image = CGImageCreate((size_t)use_w, (size_t)use_h, 8, 32,
        (size_t)(use_w * 4), colorSpace,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
        provider, NULL, NO, kCGRenderingIntentDefault);

    if (image) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        data->contentLayer.contents = (id)image;
        [CATransaction commit];
        CGImageRelease(image);
    }

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
}

void C89FW_set_title(C89FW_window_t* window, const char* title) {
    C89FW_mac_data_t* data;
    NSString* titleString;
    if (!window || !window->internal) return;
    data = (C89FW_mac_data_t*)window->internal;
    titleString = [NSString stringWithUTF8String:title];
    [data->ns_window setTitle:titleString];
}

void C89FW_set_size(C89FW_window_t* window, int width, int height) {
    C89FW_mac_data_t* data;
    NSRect frame;
    if (!window || !window->internal) return;
    data = (C89FW_mac_data_t*)window->internal;
    frame = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);
    [data->ns_window setFrame:frame display:YES];
}

C89FW_native_handles_t C89FW_get_native_handles(const C89FW_window_t* window) {
    C89FW_native_handles_t handles = {0};
    if (window && window->internal) {
        C89FW_mac_data_t* data = (C89FW_mac_data_t*)window->internal;
        handles.ns_window = data->ns_window;
        handles.ns_view = data->ns_view;
    }
    return handles;
}

#endif /* C89FW_MACOS */

#endif /* C89FW_IMPLEMENTATION_DONE */
#endif /* C89FW_IMPLEMENTATION */