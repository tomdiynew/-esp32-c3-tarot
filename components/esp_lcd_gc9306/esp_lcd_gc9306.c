
#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_lcd_gc9306.h"

#define LCD_OPCODE_WRITE_CMD   (0x02ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const char* TAG = "gc9306";

static esp_err_t panel_gc9306_del(esp_lcd_panel_t* panel);
static esp_err_t panel_gc9306_reset(esp_lcd_panel_t* panel);
static esp_err_t panel_gc9306_init(esp_lcd_panel_t* panel);
static esp_err_t panel_gc9306_draw_bitmap(esp_lcd_panel_t* panel, int x_start, int y_start, int x_end, int y_end, const void* color_data);
static esp_err_t panel_gc9306_invert_color(esp_lcd_panel_t* panel, bool invert_color_data);
static esp_err_t panel_gc9306_mirror(esp_lcd_panel_t* panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_gc9306_swap_xy(esp_lcd_panel_t* panel, bool swap_axes);
static esp_err_t panel_gc9306_set_gap(esp_lcd_panel_t* panel, int x_gap, int y_gap);
static esp_err_t panel_gc9306_disp_on_off(esp_lcd_panel_t* panel, bool off);

typedef struct
{
    esp_lcd_panel_t           base;
    esp_lcd_panel_io_handle_t io;
    int                       reset_gpio_num;
    int                       x_gap;
    int                       y_gap;
    uint8_t                   fb_bits_per_pixel;
    uint8_t                   madctl_val;    // save current value of LCD_CMD_MADCTL register
    uint8_t                   colmod_cal;    // save surrent value of LCD_CMD_COLMOD register
    const lcd_init_cmd_t*     init_cmds;
    uint16_t                  init_cmds_size;
    struct
    {
        unsigned int quad_mode   : 1;
        unsigned int reset_level : 1;
    } flags;
} gc9306_panel_t;

esp_err_t esp_lcd_new_panel_gc9306(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t* panel_dev_config, esp_lcd_panel_handle_t* ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t         ret      = ESP_OK;
    gc9306_panel_t* gc9306 = NULL;
    gc9306                   = calloc(1, sizeof(gc9306_panel_t));
    ESP_GOTO_ON_FALSE(gc9306, ESP_ERR_NO_MEM, err, TAG, "no mem for gc9306 panel");

    if(panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    // IDF >= 5.x: color_space was replaced by rgb_ele_order
    switch(panel_dev_config->rgb_ele_order)
    {
        case LCD_RGB_ELEMENT_ORDER_RGB:
            gc9306->madctl_val = 0;
            break;
        case LCD_RGB_ELEMENT_ORDER_BGR:
            gc9306->madctl_val |= LCD_CMD_BGR_BIT;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color element order");
            break;
    }

    uint8_t fb_bits_per_pixel = 0;
    switch(panel_dev_config->bits_per_pixel)
    {
        case 16:    // RGB565
            gc9306->colmod_cal = 0x55;
            fb_bits_per_pixel    = 16;
            break;
        case 18:    // RGB666
            gc9306->colmod_cal = 0x66;
            // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
            fb_bits_per_pixel = 24;
            break;
        case 24:    // RGB888
            gc9306->colmod_cal = 0x77;
            fb_bits_per_pixel    = 24;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
            break;
    }

    gc9306->io                = io;
    gc9306->fb_bits_per_pixel = fb_bits_per_pixel;
    gc9306->reset_gpio_num    = panel_dev_config->reset_gpio_num;
    gc9306->flags.reset_level = panel_dev_config->flags.reset_active_high;

    // GC9306 has no usable built-in default sequence: the vendor init table is mandatory
    gc9306_vendor_config_t* vendor_config = (gc9306_vendor_config_t*)panel_dev_config->vendor_config;
    ESP_GOTO_ON_FALSE(vendor_config && vendor_config->init_cmds && vendor_config->init_cmds_size, ESP_ERR_INVALID_ARG, err, TAG,
                      "vendor_config with init_cmds is required");
    gc9306->flags.quad_mode = vendor_config->flags.quad_mode;
    gc9306->init_cmds       = vendor_config->init_cmds;
    gc9306->init_cmds_size  = vendor_config->init_cmds_size;

    gc9306->base.del          = panel_gc9306_del;
    gc9306->base.reset        = panel_gc9306_reset;
    gc9306->base.init         = panel_gc9306_init;
    gc9306->base.draw_bitmap  = panel_gc9306_draw_bitmap;
    gc9306->base.invert_color = panel_gc9306_invert_color;
    gc9306->base.set_gap      = panel_gc9306_set_gap;
    gc9306->base.mirror       = panel_gc9306_mirror;
    gc9306->base.swap_xy      = panel_gc9306_swap_xy;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    gc9306->base.disp_off = panel_gc9306_disp_on_off;
#else
    gc9306->base.disp_on_off = panel_gc9306_disp_on_off;
#endif
    *ret_panel = &(gc9306->base);
    ESP_LOGD(TAG, "new gc9306 panel @%p", gc9306);

    return ESP_OK;

err:
    if(gc9306)
    {
        if(panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(gc9306);
    }
    return ret;
}

static esp_err_t tx_param(gc9306_panel_t* gc9306, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void* param, size_t param_size)
{
    if(gc9306->flags.quad_mode)
    {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(gc9306_panel_t* gc9306, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void* param, size_t param_size)
{
    if(gc9306->flags.quad_mode)
    {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

static esp_err_t panel_gc9306_del(esp_lcd_panel_t* panel)
{
    gc9306_panel_t* gc9306 = __containerof(panel, gc9306_panel_t, base);

    if(gc9306->reset_gpio_num >= 0)
    {
        gpio_reset_pin(gc9306->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del gc9306 panel @%p", gc9306);
    free(gc9306);
    return ESP_OK;
}

static esp_err_t panel_gc9306_reset(esp_lcd_panel_t* panel)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;

    // perform hardware reset
    if(gc9306->reset_gpio_num >= 0)
    {
        gpio_set_level(gc9306->reset_gpio_num, gc9306->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(6));
        gpio_set_level(gc9306->reset_gpio_num, !gc9306->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(160));
    }
    else
    {    // perform software reset
        tx_param(gc9306, io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20));    // spec, wait at least 5ms before sending new command
    }

    return ESP_OK;
}

static esp_err_t panel_gc9306_init(esp_lcd_panel_t* panel)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;

    // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
    tx_param(gc9306, io, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    tx_param(gc9306, io, LCD_CMD_MADCTL, (uint8_t[]){
                                               gc9306->madctl_val,
                                           },
             1);
    tx_param(gc9306, io, LCD_CMD_COLMOD, (uint8_t[]){
                                               gc9306->colmod_cal,
                                           },
             1);

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    lcd_init_cmd_t* init_cmds      = (lcd_init_cmd_t*)gc9306->init_cmds;
    uint16_t        init_cmds_size = gc9306->init_cmds_size;
    for(int i = 0; i < init_cmds_size; i++)
    {
        tx_param(gc9306, io, init_cmds[i].cmd, init_cmds[i].param, init_cmds[i].param_bytes);
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    return ESP_OK;
}

static esp_err_t panel_gc9306_draw_bitmap(esp_lcd_panel_t* panel, int x_start, int y_start, int x_end, int y_end, const void* color_data)
{
    gc9306_panel_t* gc9306 = __containerof(panel, gc9306_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = gc9306->io;

    x_start += gc9306->x_gap;
    x_end += gc9306->x_gap;
    y_start += gc9306->y_gap;
    y_end += gc9306->y_gap;

    // printf("driver draw:(%d,%d)-(%d,%d)\n", x_start, y_start, x_end, y_end);

    // define an area of frame memory where MCU can access
    tx_param(gc9306, io, LCD_CMD_CASET, (uint8_t[]){
                                              (x_start >> 8) & 0xFF,
                                              x_start & 0xFF,
                                              ((x_end - 1) >> 8) & 0xFF,
                                              (x_end - 1) & 0xFF,
                                          },
             4);
    tx_param(gc9306, io, LCD_CMD_RASET, (uint8_t[]){
                                              (y_start >> 8) & 0xFF,
                                              y_start & 0xFF,
                                              ((y_end - 1) >> 8) & 0xFF,
                                              (y_end - 1) & 0xFF,
                                          },
             4);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * gc9306->fb_bits_per_pixel / 8;
    tx_color(gc9306, io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_gc9306_invert_color(esp_lcd_panel_t* panel, bool invert_color_data)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;
    int                       command  = 0;
    if(invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    tx_param(gc9306, io, command, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_gc9306_mirror(esp_lcd_panel_t* panel, bool mirror_x, bool mirror_y)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;
    if(mirror_x)
    {
        gc9306->madctl_val |= LCD_CMD_MX_BIT;
    }
    else
    {
        gc9306->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if(mirror_y)
    {
        gc9306->madctl_val |= LCD_CMD_MY_BIT;
    }
    else
    {
        gc9306->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    tx_param(gc9306, io, LCD_CMD_MADCTL, (uint8_t[]){gc9306->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_gc9306_swap_xy(esp_lcd_panel_t* panel, bool swap_axes)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;
    if(swap_axes)
    {
        gc9306->madctl_val |= LCD_CMD_MV_BIT;
    }
    else
    {
        gc9306->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    tx_param(gc9306, io, LCD_CMD_MADCTL, (uint8_t[]){gc9306->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_gc9306_set_gap(esp_lcd_panel_t* panel, int x_gap, int y_gap)
{
    gc9306_panel_t* gc9306 = __containerof(panel, gc9306_panel_t, base);
    gc9306->x_gap            = x_gap;
    gc9306->y_gap            = y_gap;
    return ESP_OK;
}

static esp_err_t panel_gc9306_disp_on_off(esp_lcd_panel_t* panel, bool on_off)
{
    gc9306_panel_t*         gc9306 = __containerof(panel, gc9306_panel_t, base);
    esp_lcd_panel_io_handle_t io       = gc9306->io;
    int                       command  = 0;

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    on_off = !on_off;
#endif

    if(on_off)
    {
        command = LCD_CMD_DISPON;
    }
    else
    {
        command = LCD_CMD_DISPOFF;
    }
    tx_param(gc9306, io, command, NULL, 0);
    return ESP_OK;
}
