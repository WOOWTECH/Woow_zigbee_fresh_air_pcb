idf_component_register(SRCS "app_main.c" "fa_zigbee.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES fa_core nvs_flash esp_driver_gpio esp_timer)
