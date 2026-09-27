#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

typedef enum {
    LCD_MODEL_FPC_1502401,  // GC9306
    LCD_MODEL_FPC_1502403,  // GC9307
    LCD_MODEL_UNKNOWN,
} lcd_model_t;

/* Init SPI bus + GC9306 panel. Backlight is left OFF; call lcd_backlight(true) after the first frame. */
esp_err_t lcd_init(esp_lcd_panel_handle_t* ret_panel, esp_lcd_panel_io_handle_t* ret_io, size_t max_transfer_bytes);

void lcd_backlight(bool on);

/* Panel model in use (valid after lcd_init). With auto-detect this falls back to
 * FPC-1502401 when the ID can't be read. */
lcd_model_t lcd_get_model(void);

/* Panel identified from the controller ID at boot; LCD_MODEL_UNKNOWN if the ID
 * couldn't be read or didn't match (or ID reading is disabled in menuconfig). */
lcd_model_t lcd_get_detected_model(void);
const char* lcd_model_name(lcd_model_t model);
