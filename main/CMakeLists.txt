idf_component_register(SRCS "main.c" "box_model.c" "box_protocol.c" "box_config.c"
                           "box_provision.c" "box_led.c" "box_network.c" "box_display.c" "oled.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_uart esp_driver_i2c
                                     esp_lcd esp_http_client esp_wifi esp_netif esp_event esp_timer
                                     nvs_flash mbedtls json esp_system esp_hw_support)
