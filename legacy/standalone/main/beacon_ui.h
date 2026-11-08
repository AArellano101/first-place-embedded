#pragma once
#include "beacon.h"
typedef struct { char lines[8][22]; } beacon_screen_t;
void beacon_format_screen(const beacon_t *b, int64_t now_ms, beacon_screen_t *screen);
