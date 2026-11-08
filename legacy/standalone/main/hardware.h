#pragma once
#include "beacon.h"
#include "esp_err.h"

void hardware_init(void);
bool hardware_button_pressed(button_t button);
void hardware_set_availability(availability_t availability);
void hardware_set_buzzer(bool on);
esp_err_t hardware_read_pot(int *raw);
