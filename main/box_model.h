#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "box_settings.h"

typedef enum { LIGHT_OFF, LIGHT_GREEN, LIGHT_YELLOW, LIGHT_RED } box_light_t;
typedef enum { BOX_WAITING, BOX_FRESH, BOX_NO_SIGNAL, BOX_CONFIG_FAULT } box_health_t;
typedef struct {
    box_light_t light;
    bool valid;
    bool config_fault;
    int64_t received_ms;
} box_update_t;
typedef struct {
    box_light_t light;
    bool ever_valid, config_fault;
    int64_t boot_ms, last_valid_ms, pulse_started_ms;
} box_model_t;
typedef struct {
    unsigned red, yellow, green; // logical 0..1023 duty, before wiring polarity
    bool status_on;
    box_health_t health;
    box_light_t light;
} box_output_t;
void box_model_init(box_model_t *model, int64_t now_ms);
void box_model_update(box_model_t *model, const box_update_t *update);
box_output_t box_model_output(const box_model_t *model, int64_t now_ms,
                              unsigned brightness, unsigned pulse_ms);
unsigned box_retry_delay(unsigned failures, unsigned random_value);
const char *box_light_name(box_light_t light);
