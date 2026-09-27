/*
 * 1.50" 240x280 FPC-1502401 (GC9306 + CST816D) on ESP32-C3 SuperMini
 *
 * Pin choice:
 *  - LCD SCLK/MOSI/CS use the SPI2 IO_MUX pins (GPIO6/7/10), so the clock can go up to 80 MHz.
 *  - Strapping pins GPIO2/8/9 are left unused (GPIO8 = on-board LED, GPIO9 = BOOT key on SuperMini).
 *  - GPIO18/19 are the USB D-/D+ pins (USB-Serial-JTAG, used for flashing & log) -> never touch.
 *  - GPIO20/21 are UART0 RX/TX by default; the console is moved to USB-Serial-JTAG in
 *    sdkconfig.defaults so they can be used for touch INT/RST.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* LCD (GC9306, 4-line SPI)          FPC pin */
#define BOARD_LCD_SCLK   GPIO_NUM_6  // 5  LCD_SCL
#define BOARD_LCD_MOSI   GPIO_NUM_7  // 7  LCD_SDA
#define BOARD_LCD_CS     GPIO_NUM_10 // 4  LCD_CS
#define BOARD_LCD_DC     GPIO_NUM_5  // 24 LCD_DC
#define BOARD_LCD_RST    GPIO_NUM_4  // 1  LCD_RES
#define BOARD_LCD_BL     GPIO_NUM_3  // gate of the backlight MOSFET (LED_K side), -1 if not used
#define BOARD_LCD_BL_ON_LEVEL 1      // adapter board: N-MOSFET low-side switch, high = on

/* Touch (CST816D, I2C) */
#define BOARD_TP_SCL     GPIO_NUM_1  // 20 TP_SCL
#define BOARD_TP_SDA     GPIO_NUM_0  // 19 TP_SDA
#define BOARD_TP_INT     GPIO_NUM_20 // 17 TP_INT
#define BOARD_TP_RST     GPIO_NUM_21 // 18 TP_RES

/* Menu key: the on-board BOOT key (GPIO9 -> GND when pressed). It is a strapping pin, so holding it
 * during reset/power-up enters download mode; once the app runs it is an ordinary input. */
#define BOARD_BTN              GPIO_NUM_9
#define BOARD_BTN_ACTIVE_LEVEL 0

#define BOARD_LCD_SPI_HOST  SPI2_HOST
#define BOARD_LCD_SPI_HZ    (40 * 1000 * 1000) // vendor used 60 MHz on S3; 40 MHz is safer with jumper wires
#define BOARD_TP_I2C_HZ     (400 * 1000)

/* Panel geometry (from vendor bsp: USE_SPI_LCD_150_GC9306) */
#define BOARD_LCD_H_RES  240
#define BOARD_LCD_V_RES  280
#define BOARD_LCD_X_GAP  0
#define BOARD_LCD_Y_GAP  20  // GRAM is 240x320, visible 280 rows start at row 20
