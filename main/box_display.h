#pragma once
#include "box_model.h"
#include "esp_err.h"
typedef struct { box_health_t health; box_light_t light; bool provisioned; } box_screen_t;
esp_err_t box_display_start(void);
void box_display_submit(const box_screen_t *screen);
