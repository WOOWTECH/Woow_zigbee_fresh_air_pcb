idf_component_register(SRCS "app_main.c" "fa_matter.cpp" "st25dv.c" "fa_nfc_port.c" "ws2812.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES fa_core esp_matter nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt
                                     esp_timer esp_hw_support bootloader_support)
