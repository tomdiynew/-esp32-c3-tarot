#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

/* The on-board BOOT key (GPIO9) as the only input.
 * Menus use the gestures:
 *  - short press  : next item (reported BUTTON_DOUBLE_MS after release, if no second click came)
 *  - long press   : confirm (fires while still held, after BUTTON_LONG_MS)
 *  - double click : back
 * Games use the raw edges (BUTTON_DOWN / BUTTON_UP, no delay) and button_held_ms(). */

#define BUTTON_LONG_MS   1000
#define BUTTON_DOUBLE_MS 300  // max gap between the two clicks of a double click

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_DOWN,
    BUTTON_UP,
    BUTTON_SHORT,
    BUTTON_LONG,
    BUTTON_DOUBLE,
} button_event_t;

/* Configure the pin and start the polling task */
void button_init(void);

/* Next event, waiting up to `timeout` ticks (0 = just check); BUTTON_NONE if there is none */
button_event_t button_get(TickType_t timeout);

/* Drop queued events, and ignore the press in progress (if any) until the key is released */
void button_flush(void);

/* How long the key has been held down, 0 when up (or when the press is being ignored) */
uint32_t button_held_ms(void);
