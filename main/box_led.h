#pragma once
#include "box_model.h"
#include "esp_err.h"
esp_err_t box_led_init(void);
void box_led_apply(const box_output_t *output);
