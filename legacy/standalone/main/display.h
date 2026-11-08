#pragma once
#include "beacon_ui.h"
#include "esp_err.h"
esp_err_t display_start(void);
// Nonblocking copy; display task exclusively owns the OLED and I2C bus.
void display_submit(const beacon_screen_t *screen);
