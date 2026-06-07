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
                printf("[INPUT] key pressed: key=%d\n", event.data.key.code);
                break;
            case C89FW_EVENT_KEY_UP:
                printf("[INPUT] key released: key=%d\n", event.data.key.code);
                break;
            case C89FW_EVENT_MOUSE_DOWN:
                printf("[INPUT] mouse button pressed: button=%d at (%d, %d)\n",
                       event.data.mouse_button.button, event.data.mouse_button.x, event.data.mouse_button.y);
                break;
            case C89FW_EVENT_MOUSE_UP:
                printf("[INPUT] mouse button released: button=%d at (%d, %d)\n",
                       event.data.mouse_button.button, event.data.mouse_button.x, event.data.mouse_button.y);
                break;
            case C89FW_EVENT_MOUSE_MOVE:
                printf("[INPUT] mouse moved: (%d, %d) delta=(%d, %d)\n",
                       event.data.mouse_move.x, event.data.mouse_move.y,
                       event.data.mouse_move.delta_x, event.data.mouse_move.delta_y);
                break;
            case C89FW_EVENT_MOUSE_SCROLL:
                printf("[INPUT] mouse wheel: scroll=(%f, %f)\n",
                       event.data.mouse_scroll.delta_x, event.data.mouse_scroll.delta_y);
                break;
            case C89FW_EVENT_RESIZE:
                printf("[INPUT] window resized: (%d, %d)\n",
                       event.data.resize.width, event.data.resize.height);
                /* Wait for pending present before resize to avoid use-after-free */
                thpool_wait(render_threadpool);
                /* window_resize handles renderer resize + C89FW_apply_resize */
                window_resize(event.data.resize.width, event.data.resize.height);
                if (w) scenario_resize(w, event.data.resize.width, event.data.resize.height);
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
                printf("[INPUT] gamepad %d connected\n", event.data.gamepad_button.gamepad_index);
                break;
            case C89FW_EVENT_GAMEPAD_DISCONNECT:
                printf("[INPUT] gamepad %d disconnected\n", event.data.gamepad_button.gamepad_index);
                break;
            case C89FW_EVENT_GAMEPAD_DOWN:
                printf("[INPUT] gamepad %d button %d pressed\n",
                       event.data.gamepad_button.gamepad_index, event.data.gamepad_button.button);
                break;
            case C89FW_EVENT_GAMEPAD_UP:
                printf("[INPUT] gamepad %d button %d released\n",
                       event.data.gamepad_button.gamepad_index, event.data.gamepad_button.button);
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
