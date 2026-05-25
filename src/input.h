#ifndef INPUT_H
#define INPUT_H

#include "window.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Process input events - call from main loop, logs to console */
static void input_process_events(RGFW_window *win)
{
    RGFW_event event;
    while (RGFW_window_checkEvent(win, &event)) {
        switch (event.type) {
            case RGFW_keyPressed:
            case RGFW_keyReleased:
                printf("[INPUT] key %s: key=%d\n",
                       event.type == RGFW_keyPressed ? "pressed" : "released",
                       event.key.value);
                break;
            case RGFW_mouseButtonPressed:
            case RGFW_mouseButtonReleased:
                printf("[INPUT] mouse button %s: button=%d\n",
                       event.type == RGFW_mouseButtonPressed ? "pressed" : "released",
                       event.button.value);
                break;
            case RGFW_mouseMotion:
                printf("[INPUT] mouse moved: (%d, %d)\n", event.mouse.x, event.mouse.y);
                break;
            case RGFW_mouseEnter:
                printf("[INPUT] mouse entered at (%d, %d)\n", event.mouse.x, event.mouse.y);
                break;
            case RGFW_mouseScroll:
                printf("[INPUT] mouse wheel: scroll=(%f, %f)\n", event.delta.x, event.delta.y);
                break;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */