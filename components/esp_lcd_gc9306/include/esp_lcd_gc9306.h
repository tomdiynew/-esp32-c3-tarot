/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "hal/spi_ll.h"
#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 *
 */
typedef struct
{
    uint8_t        cmd;            // Command byte.
    const uint8_t* param;          // Pointer to parameters.
    uint8_t        param_bytes;    // Bytes of parameters.
    uint16_t       delay_ms;       // Delay in milliseconds after this command.
} lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 * @note  This structure can be used to enable QSPI mode and override default initialization commands.
 * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
 *
 */
typedef struct
{
    const lcd_init_cmd_t* init_cmds;    // Pointer to initialization commands array.
                                        // The array should be declared as static const and positioned outside the function.
                                        // Please refer to `vendor_specific_init_default` in `esp_lcd_gc9306.c`.
    uint16_t init_cmds_size;            // Number of commands in above array.

    struct
    {
        unsigned int quad_mode              : 1;    // Set to 1 if use quad mode (4 data line).
        unsigned int use_external_init_cmds : 1;    // Set to 1 if use external initialization commands instead of internal default commands.
    } flags;
} gc9306_vendor_config_t;

/**
 * @brief Create LCD panel for model GC9306
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config General panel device configuration (Use `vendor_config` to enable QSPI and override default initialization commands)
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *          - ESP_ERR_INVALID_ARG       if parameter is invalid
 *          - ESP_ERR_NO_MEM            if out of memory
 *          - ESP_ERR_NOT_SUPPORTED     if parameter is not supported
 *          - ESP_OK                    on success
 */
esp_err_t esp_lcd_new_panel_gc9306(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t* panel_dev_config, esp_lcd_panel_handle_t* ret_panel);

/**
 * @brief LCD bus configuration structure
 *
 */
#define ESP_LCD_BUS_SPI_GC9306_CONFIG(sclk, mosi)     \
    {                                                   \
        .sclk_io_num     = sclk,                        \
        .mosi_io_num     = mosi,                        \
        .miso_io_num     = -1,                          \
        .quadhd_io_num   = -1,                          \
        .quadwp_io_num   = -1,                          \
        .max_transfer_sz = SPI_LL_DMA_MAX_BIT_LEN >> 3, \
    }
#define ESP_LCD_BUS_QSPI_GC9306_CONFIG(sclk, d0, d1, d2, d3) \
    {                                                          \
        .sclk_io_num     = sclk,                               \
        .data0_io_num    = d0,                                 \
        .data1_io_num    = d1,                                 \
        .data2_io_num    = d2,                                 \
        .data3_io_num    = d3,                                 \
        .max_transfer_sz = SPI_LL_DMA_MAX_BIT_LEN >> 3,        \
    }

/**
 * @brief LCD IO configuration structure
 *
 */
#define ESP_LCD_IO_SPI_GC9306_CONFIG(cs, dc, cb, cb_ctx) \
    {                                                      \
        .cs_gpio_num         = cs,                         \
        .dc_gpio_num         = dc,                         \
        .spi_mode            = 0,                          \
        .pclk_hz             = 60 * 1000 * 1000,           \
        .trans_queue_depth   = 40,                         \
        .on_color_trans_done = cb,                         \
        .user_ctx            = cb_ctx,                     \
        .lcd_cmd_bits        = 8,                          \
        .lcd_param_bits      = 8,                          \
    }
#define ESP_LCD_IO_QSPI_GC9306_CONFIG(cs, cb, cb_ctx) \
    {                                                   \
        .cs_gpio_num         = cs,                      \
        .dc_gpio_num         = -1,                      \
        .spi_mode            = 0,                       \
        .pclk_hz             = 80 * 1000 * 1000,        \
        .trans_queue_depth   = 10,                      \
        .on_color_trans_done = cb,                      \
        .user_ctx            = cb_ctx,                  \
        .lcd_cmd_bits        = 32,                      \
        .lcd_param_bits      = 8,                       \
        .flags               = {                        \
                          .quad_mode = true,            \
        },                                \
    }

#ifdef __cplusplus
}
#endif
