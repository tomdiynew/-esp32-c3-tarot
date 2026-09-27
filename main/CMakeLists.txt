idf_component_register(SRCS "main.c" "lcd.c" "button.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "art/logo.rgb565"
                                   "art/bg.rgb565"
                                   "art/sprites.bin"
                       REQUIRES esp_lcd esp_lcd_gc9306 esp_driver_spi esp_driver_gpio esp_timer esp_hw_support)
