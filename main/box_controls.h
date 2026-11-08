#pragma once
#include "box_timer.h"
#include "esp_err.h"

esp_err_t box_controls_start(void);
void box_controls_snapshot(box_timer_t *timer);
