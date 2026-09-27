#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "lcd.h"

static const char* TAG = "lcd";

/*
 * GC9306 init sequence, copied unchanged from the vendor project
 * (lcd_test/components/bsp/bsp.c, USE_SPI_LCD_150_GC9306).
 * The driver sends SLPOUT(+100ms), MADCTL and COLMOD before this table, DISPON after it.
 */
static const lcd_init_cmd_t gc9306_150_init_cmds[] = {
//   cmd   data                                               len delay_ms
    {0xfe, (uint8_t[]){0},                                    0,  0},  // inter register enable 1
    {0xef, (uint8_t[]){0},                                    0,  0},  // inter register enable 2
    {0x36, (uint8_t[]){0x48},                                 1,  0},  // MADCTL: MX | BGR
    {0x3a, (uint8_t[]){0x05},                                 1,  0},  // COLMOD: RGB565
    {0x35, (uint8_t[]){0x00},                                 1,  0},  // TE on (pin not used here)
    {0x44, (uint8_t[]){0x00, 0x60},                           2,  0},  // TE scanline
    {0xa4, (uint8_t[]){0x44, 0x44},                           2,  0},
    {0xa5, (uint8_t[]){0x42, 0x42},                           2,  0},
    {0xaa, (uint8_t[]){0x88, 0x88},                           2,  0},
    {0xe8, (uint8_t[]){0x11, 0x71},                           2,  0},
    {0xe3, (uint8_t[]){0x01, 0x10},                           2,  0},
    {0xff, (uint8_t[]){0x61},                                 1,  0},
    {0xAC, (uint8_t[]){0x00},                                 1,  0},
    {0xAe, (uint8_t[]){0x2b},                                 1,  0},
    {0xAd, (uint8_t[]){0x33},                                 1,  0},
    {0xAf, (uint8_t[]){0x55},                                 1,  0},
    {0xa6, (uint8_t[]){0x2a, 0x2a},                           2,  0},
    {0xa7, (uint8_t[]){0x2b, 0x2b},                           2,  0},
    {0xa8, (uint8_t[]){0x18, 0x18},                           2,  0},
    {0xa9, (uint8_t[]){0x2a, 0x2a},                           2,  0},
    {0xf0, (uint8_t[]){0x02, 0x01, 0x00, 0x00, 0x02, 0x09},   6,  0},  // gamma
    {0xf1, (uint8_t[]){0x01, 0x02, 0x00, 0x11, 0x1c, 0x15},   6,  0},
    {0xf2, (uint8_t[]){0x0a, 0x07, 0x29, 0x04, 0x04, 0x38},   6,  0},
    {0xf3, (uint8_t[]){0x15, 0x0d, 0x55, 0x04, 0x03, 0x65},   6,  0},
    {0xf4, (uint8_t[]){0x0f, 0x1d, 0x1e, 0x0a, 0x0d, 0x0f},   6,  0},
    {0xf5, (uint8_t[]){0x05, 0x12, 0x11, 0x34, 0x34, 0x0f},   6,  0},
    // vendor table ended with 0x21 (inversion ON); now sent by lcd_init() according to CONFIG_LCD_INVERT_*
};

/*
 * GC9307 init sequence from the vendor project
 * (lcd_test/components/bsp/bsp.c, shared by USE_SPI_LCD_170_GC9307 / 183 / 201 / 181),
 * with one change: B6h line count raised from 296 to 320 (see below).
 * FPC-1502403 reads back ID 00 93 07 = GC9307. The 1.70" FPC-1702430 using this table is also
 * 240x280 with a 20-row offset, and the vendor runs these panels without inversion.
 */
static const lcd_init_cmd_t gc9307_init_cmds[] = {
//   cmd   data                                               len delay_ms
    {0x36, (uint8_t[]){0x00},                                 1,  0},
    {0x3A, (uint8_t[]){0x05},                                 1,  0},
    {0xfe, (uint8_t[]){0},                                    0,  0},  // inter register enable 1
    {0xef, (uint8_t[]){0},                                    0,  0},  // inter register enable 2
    {0x36, (uint8_t[]){0x48},                                 1,  0},  // MADCTL: MX | BGR
    {0x3a, (uint8_t[]){0x05},                                 1,  0},  // COLMOD: RGB565
    {0x84, (uint8_t[]){0x40},                                 1,  0},
    {0x86, (uint8_t[]){0x98},                                 1,  0},
    {0x89, (uint8_t[]){0x13},                                 1,  0},
    {0x8b, (uint8_t[]){0x80},                                 1,  0},
    {0x8d, (uint8_t[]){0x33},                                 1,  0},
    {0x8e, (uint8_t[]){0x0f},                                 1,  0},
    /* Display Function Control, 3rd param NL = number of gate lines driven (GC9307 DS p.154).
     * Vendor value 0x24 = 296 lines (made for their 240x296 panel): with our 20-row offset the
     * visible rows are GRAM 20..299, so rows 296..299 (the bottom 4 on screen) were never driven.
     * 0x27 = 320 lines, the chip's reset default, covers all of GRAM. */
    {0xb6, (uint8_t[]){0x00, 0x00, 0x27},                     3,  0},
    {0xe8, (uint8_t[]){0x13, 0x00},                           2,  0},
    {0xEC, (uint8_t[]){0x33, 0x00, 0xF0},                     3,  0},
    {0xff, (uint8_t[]){0x62},                                 1,  0},
    {0x99, (uint8_t[]){0x3e},                                 1,  0},
    {0x9d, (uint8_t[]){0x4b},                                 1,  0},
    {0x98, (uint8_t[]){0x3e},                                 1,  0},
    {0x9c, (uint8_t[]){0x4b},                                 1,  0},
    {0xc3, (uint8_t[]){0x2A},                                 1,  0},
    {0xc4, (uint8_t[]){0x14},                                 1,  0},
    {0xc9, (uint8_t[]){0x34},                                 1,  0},
    {0xF0, (uint8_t[]){0x1D, 0x21, 0x0C, 0x0B, 0x06, 0x43},   6,  0},  // gamma
    {0xF1, (uint8_t[]){0x56, 0x78, 0x94, 0x2C, 0x2D, 0xAF},   6,  0},
    {0xF2, (uint8_t[]){0x1D, 0x21, 0x0C, 0x0B, 0x06, 0x43},   6,  0},
    {0xF3, (uint8_t[]){0x56, 0x78, 0x94, 0x2C, 0x2D, 0xAF},   6,  0},
};

/* Per-model settings, picked at boot from the menuconfig choice or the controller ID */
typedef struct {
    const char*           name;
    const lcd_init_cmd_t* cmds;
    size_t                n_cmds;
    bool                  invert;
} lcd_model_cfg_t;

static const lcd_model_cfg_t s_models[] = {
    [LCD_MODEL_FPC_1502401] = {"FPC-1502401 (GC9306)", gc9306_150_init_cmds,
                               sizeof(gc9306_150_init_cmds) / sizeof(gc9306_150_init_cmds[0]), true},
    [LCD_MODEL_FPC_1502403] = {"FPC-1502403 (GC9307)", gc9307_init_cmds,
                               sizeof(gc9307_init_cmds) / sizeof(gc9307_init_cmds[0]), false},
};

static lcd_model_t s_model    = LCD_MODEL_UNKNOWN;
static lcd_model_t s_detected = LCD_MODEL_UNKNOWN;

lcd_model_t lcd_get_model(void)
{
    return s_model;
}

lcd_model_t lcd_get_detected_model(void)
{
    return s_detected;
}

const char* lcd_model_name(lcd_model_t model)
{
    return model == LCD_MODEL_UNKNOWN ? "unknown" : s_models[model].name;
}

#if CONFIG_LCD_MODEL_AUTO || CONFIG_LCD_READ_ID
/*
 * Read the controller ID over a temporary 1 MHz link: GC9306 reads need SCL cycle >= 150 ns
 * (datasheet table 47), so they fail at the normal 40 MHz. SDA is bidirectional (sio_mode).
 *  - DAh/DBh/DCh: 8-bit reads, no dummy bit.   GC9306: DB=93 DC=06
 *  - 04h: one dummy clock bit, then 24 bits.    GC9306: 00 93 06
 * Returns the panel model that matches the controller, or LCD_MODEL_UNKNOWN.
 */
static lcd_model_t lcd_read_id(void)
{
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num       = BOARD_LCD_CS,
        .dc_gpio_num       = BOARD_LCD_DC,
        .spi_mode          = 0,
        .pclk_hz           = 1 * 1000 * 1000,
        .trans_queue_depth = 1,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .flags.sio_mode    = true,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_config, &io) != ESP_OK) {
        ESP_LOGW(TAG, "read id: cannot create slow SPI device");
        return LCD_MODEL_UNKNOWN;
    }

    /* hardware reset so the controller is in a known state */
    const gpio_config_t rst_conf = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << BOARD_LCD_RST};
    gpio_config(&rst_conf);
    gpio_set_level(BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t id1 = 0, id2 = 0, id3 = 0, raw[4] = {0};
    esp_lcd_panel_io_rx_param(io, 0xDA, &id1, 1);
    esp_lcd_panel_io_rx_param(io, 0xDB, &id2, 1);
    esp_lcd_panel_io_rx_param(io, 0xDC, &id3, 1);
    esp_lcd_panel_io_rx_param(io, 0x04, raw, sizeof(raw));
    esp_lcd_panel_io_del(io);

    uint32_t bits  = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | raw[3];
    uint32_t rddid = (bits >> 7) & 0xFFFFFF;  // drop the dummy bit
    ESP_LOGI(TAG, "ID  DA/DB/DC = %02X %02X %02X,  04h = %06" PRIX32 " (raw %02X %02X %02X %02X)", id1, id2, id3, rddid,
             raw[0], raw[1], raw[2], raw[3]);
    if (id2 == 0x93 && id3 == 0x06) {
        ESP_LOGI(TAG, "controller: GC9306 -> FPC-1502401");
        return LCD_MODEL_FPC_1502401;
    }
    if (id2 == 0x93 && id3 == 0x07) {
        ESP_LOGI(TAG, "controller: GC9307 -> FPC-1502403");
        return LCD_MODEL_FPC_1502403;
    }
    if ((id1 | id2 | id3) == 0 || (id1 & id2 & id3) == 0xFF) {
        ESP_LOGW(TAG, "controller: no answer (all %02X) - check LCD_SDA wiring", id1);
    } else {
        ESP_LOGW(TAG, "controller: unknown ID - no matching init table");
    }
    return LCD_MODEL_UNKNOWN;
}
#endif

/* Decide which panel settings to use: auto-detect, or the menuconfig model (warn if the ID disagrees) */
static lcd_model_t lcd_pick_model(void)
{
    lcd_model_t detected = LCD_MODEL_UNKNOWN;
#if CONFIG_LCD_MODEL_AUTO || CONFIG_LCD_READ_ID
    detected = lcd_read_id();
#endif
    s_detected = detected;

#if CONFIG_LCD_MODEL_AUTO
    if (detected == LCD_MODEL_UNKNOWN) {
        ESP_LOGW(TAG, "auto-detect failed, falling back to %s", lcd_model_name(LCD_MODEL_FPC_1502401));
        return LCD_MODEL_FPC_1502401;
    }
    return detected;
#else
#if CONFIG_LCD_MODEL_FPC_1502403
    const lcd_model_t chosen = LCD_MODEL_FPC_1502403;
#else
    const lcd_model_t chosen = LCD_MODEL_FPC_1502401;
#endif
    if (detected != LCD_MODEL_UNKNOWN && detected != chosen) {
        ESP_LOGW(TAG, "menuconfig says %s but the panel looks like %s", lcd_model_name(chosen), lcd_model_name(detected));
    }
    return chosen;
#endif
}

static void backlight_init(void)
{
    if (BOARD_LCD_BL < 0) {
        return;
    }
    const gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_LCD_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    lcd_backlight(false);
}

void lcd_backlight(bool on)
{
    if (BOARD_LCD_BL >= 0) {
        gpio_set_level(BOARD_LCD_BL, on ? BOARD_LCD_BL_ON_LEVEL : !BOARD_LCD_BL_ON_LEVEL);
    }
}

esp_err_t lcd_init(esp_lcd_panel_handle_t* ret_panel, esp_lcd_panel_io_handle_t* ret_io, size_t max_transfer_bytes)
{
    backlight_init();

    ESP_LOGI(TAG, "SPI bus: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d, %d MHz", BOARD_LCD_SCLK, BOARD_LCD_MOSI, BOARD_LCD_CS,
             BOARD_LCD_DC, BOARD_LCD_RST, BOARD_LCD_SPI_HZ / 1000000);
    const spi_bus_config_t buscfg = {
        .sclk_io_num     = BOARD_LCD_SCLK,
        .mosi_io_num     = BOARD_LCD_MOSI,
        .miso_io_num     = -1,
        .quadhd_io_num   = -1,
        .quadwp_io_num   = -1,
        .max_transfer_sz = max_transfer_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    s_model                    = lcd_pick_model();
    const lcd_model_cfg_t* cfg = &s_models[s_model];
#if CONFIG_LCD_INVERT_ON
    const bool invert = true;
#elif CONFIG_LCD_INVERT_OFF
    const bool invert = false;
#else
    const bool invert = cfg->invert;
#endif

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num       = BOARD_LCD_CS,
        .dc_gpio_num       = BOARD_LCD_DC,
        .spi_mode          = 0,
        .pclk_hz           = BOARD_LCD_SPI_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .flags.sio_mode    = true,  // same as vendor: SDA is bidirectional (3-wire half duplex)
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_config, &io), TAG, "panel io");

    const gc9306_vendor_config_t vendor_config = {
        .init_cmds      = cfg->cmds,
        .init_cmds_size = cfg->n_cmds,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config  = (void*)&vendor_config,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9306(io, &panel_config, &panel), TAG, "new panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, invert), TAG, "invert");
    ESP_LOGI(TAG, "panel %s, color inversion %s", cfg->name, invert ? "ON" : "OFF");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, BOARD_LCD_X_GAP, BOARD_LCD_Y_GAP), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "disp on");

    *ret_panel = panel;
    *ret_io    = io;
    return ESP_OK;
}
