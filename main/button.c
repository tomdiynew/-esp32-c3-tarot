#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board.h"
#include "button.h"

static const char* TAG = "button";

#define POLL_MS          10
#define DEBOUNCE_SAMPLES 3  // level must be stable for 30 ms

static QueueHandle_t     s_events;
static volatile uint32_t s_held_ms;
static volatile bool     s_flush_req;

static void emit(button_event_t ev)
{
    xQueueSend(s_events, &ev, 0);  // drop it if nobody is reading
}

static void button_task(void* arg)
{
    bool     pressed   = false;  // debounced state
    int      stable    = 0;      // samples the raw level has differed from `pressed`
    uint32_t held      = 0;      // ms the key has been down
    bool     long_sent = false;  // this press already produced BUTTON_LONG
    bool     ignore    = false;  // swallow everything until the key is released
    int      clicks    = 0;      // short clicks waiting for the double-click window
    uint32_t idle      = 0;      // ms since the last release

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (s_flush_req) {
            clicks      = 0;
            ignore      = pressed;
            s_held_ms   = 0;
            s_flush_req = false;  // last: button_flush() returns once this is seen
        }

        const bool raw = gpio_get_level(BOARD_BTN) == BOARD_BTN_ACTIVE_LEVEL;
        stable         = raw != pressed ? stable + 1 : 0;
        if (stable >= DEBOUNCE_SAMPLES) {
            pressed = raw;
            stable  = 0;
            if (pressed) {
                held      = 0;
                long_sent = false;
                emit(BUTTON_DOWN);
            } else if (ignore) {
                ignore = false;
            } else {
                emit(BUTTON_UP);
                if (!long_sent && ++clicks == 2) {
                    emit(BUTTON_DOUBLE);
                    clicks = 0;
                }
                idle = 0;
            }
        }

        if (pressed && !ignore) {
            held += POLL_MS;
            if (!long_sent && held >= BUTTON_LONG_MS) {
                emit(BUTTON_LONG);
                long_sent = true;
                clicks    = 0;  // a long press cancels a pending click
            }
        } else if (clicks == 1) {
            idle += POLL_MS;
            if (idle >= BUTTON_DOUBLE_MS) {
                emit(BUTTON_SHORT);
                clicks = 0;
            }
        }
        s_held_ms = pressed && !ignore ? held : 0;
    }
}

void button_init(void)
{
    const gpio_config_t conf = {
        .pin_bit_mask = 1ULL << BOARD_BTN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&conf));
    s_events = xQueueCreate(16, sizeof(button_event_t));
    assert(s_events);
    xTaskCreate(button_task, "button", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "key on GPIO%d", BOARD_BTN);
}

button_event_t button_get(TickType_t timeout)
{
    button_event_t ev;
    return xQueueReceive(s_events, &ev, timeout) == pdTRUE ? ev : BUTTON_NONE;
}

void button_flush(void)
{
    s_flush_req = true;
    while (s_flush_req) {  // let the task drop its click state first, then clear the queue
        vTaskDelay(1);
    }
    xQueueReset(s_events);
}

uint32_t button_held_ms(void)
{
    return s_held_ms;
}
