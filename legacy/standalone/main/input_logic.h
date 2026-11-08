#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { bool raw, stable; int64_t changed_ms; } debounce_t;
bool button_update(debounce_t *button, bool pressed, int64_t now_ms);
typedef struct { bool initialized; int filtered_q8; unsigned index; } dial_t;
unsigned dial_update(dial_t *dial, int raw_adc);
