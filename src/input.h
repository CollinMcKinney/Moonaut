#ifndef INPUT_H
#define INPUT_H

#include "window.h"
#include "runtime.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static void input_process_events(C89FW_window_t *win, scenario_world *w)
{
    C89FW_event_t event;
    while (C89FW_poll_event(win, &event)) {
        switch (event.type) {
            case C89FW_EVENT_KEY_DOWN:
                printf("[INPUT] key pressed: key=%d\n", event.key.code);
                break;
            case C89FW_EVENT_KEY_UP:
                printf("[INPUT] key released: key=%d\n", event.key.code);
                break;
            case C89FW_EVENT_MOUSE_DOWN:
                printf("[INPUT] mouse button pressed: button=%d at (%d, %d)\n",
                       event.mouse_button.button, event.mouse_button.x, event.mouse_button.y);
                break;
            case C89FW_EVENT_MOUSE_UP:
                printf("[INPUT] mouse button released: button=%d at (%d, %d)\n",
                       event.mouse_button.button, event.mouse_button.x, event.mouse_button.y);
                break;
            case C89FW_EVENT_MOUSE_MOVE:
                printf("[INPUT] mouse moved: (%d, %d) delta=(%d, %d)\n",
                       event.mouse_move.x, event.mouse_move.y,
                       event.mouse_move.delta_x, event.mouse_move.delta_y);
                break;
            case C89FW_EVENT_MOUSE_SCROLL:
                printf("[INPUT] mouse wheel: scroll=(%f, %f)\n",
                       event.mouse_scroll.delta_x, event.mouse_scroll.delta_y);
                break;
            case C89FW_EVENT_RESIZE:
                printf("[INPUT] window resized: (%d, %d)\n",
                       event.resize.width, event.resize.height);
                /* Wait for pending present before resize to avoid use-after-free */
                thpool_wait(render_threadpool);
                /* window_resize handles renderer resize + C89FW_apply_resize */
                window_resize(event.resize.width, event.resize.height);
                if (w) scenario_resize(w, event.resize.width, event.resize.height);
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
                printf("[INPUT] gamepad %d connected\n", event.gamepad_button.gamepad_index);
                break;
            case C89FW_EVENT_GAMEPAD_DISCONNECT:
                printf("[INPUT] gamepad %d disconnected\n", event.gamepad_button.gamepad_index);
                break;
            case C89FW_EVENT_GAMEPAD_DOWN:
                printf("[INPUT] gamepad %d button %d pressed\n",
                       event.gamepad_button.gamepad_index, event.gamepad_button.button);
                break;
            case C89FW_EVENT_GAMEPAD_UP:
                printf("[INPUT] gamepad %d button %d released\n",
                       event.gamepad_button.gamepad_index, event.gamepad_button.button);
                break;
            default:
                break;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */