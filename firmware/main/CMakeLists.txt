idf_component_register(SRCS "app_main.c" "fa_zigbee.c" "st25dv.c" "fa_nfc_port.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES fa_core nvs_flash esp_driver_gpio esp_driver_i2c esp_timer esp_hw_support bootloader_support)
