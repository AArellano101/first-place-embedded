idf_component_register(SRCS "main.c" "oled.c" "beacon.c" "input_logic.c"
                           "beacon_ui.c" "hardware.c" "display.c" "backend_adapter.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc
                                  esp_adc esp_lcd esp_timer console)
