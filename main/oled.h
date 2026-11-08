#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Single SSD1306 display, 128x32 or 128x64. Call from one task only.
esp_err_t oled_init(i2c_master_bus_handle_t bus, uint8_t address, unsigned height);
void oled_clear(void);
// 6x8 character cells: columns 0..20; rows 0..3 or 0..7.
// Printable ASCII, newline, wrapping; text beyond the bottom is clipped.
void oled_write_text(unsigned column, unsigned row, const char *text);
esp_err_t oled_show(void);
