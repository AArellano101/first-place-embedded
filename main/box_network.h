#pragma once
#include "box_config.h"
#include "box_model.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
// Queue holds box_update_t. Only the networking task owns the HTTP handle.
esp_err_t box_network_start(const box_config_t *config, QueueHandle_t updates);
