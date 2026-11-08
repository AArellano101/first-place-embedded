#include "input_logic.h"
#include "beacon_config.h"

bool button_update(debounce_t *button, bool pressed, int64_t now)
{
    if (pressed != button->raw) {
        button->raw = pressed;
        button->changed_ms = now;
    }
    if (button->stable != button->raw && now - button->changed_ms >= DEBOUNCE_MS) {
        button->stable = button->raw;
        return button->stable; // Press edge only, no repeat while held.
    }
    return false;
}

unsigned dial_update(dial_t *d, int raw)
{
    const int low = CONFIG_THIRD_PLACE_ADC_LOW, high = CONFIG_THIRD_PLACE_ADC_HIGH;
    const int span = high - low + 1, h = CONFIG_THIRD_PLACE_ADC_HYSTERESIS;
    if (raw < low) raw = low;
    if (raw > high) raw = high;
    if (!d->initialized) {
        d->filtered_q8 = raw * 256;
        d->index = (unsigned)((raw - low) * DURATION_STEPS / span);
        d->initialized = true;
    } else d->filtered_q8 += (raw * 256 - d->filtered_q8) / ADC_SMOOTH_DIVISOR;
    const int value = (d->filtered_q8 + 128) / 256;
    // Schmitt thresholds around each bin boundary; works for multi-bin moves.
    while (d->index + 1 < DURATION_STEPS && value >=
           low + (int)(((d->index + 1) * span + DURATION_STEPS - 1) / DURATION_STEPS) + h) d->index++;
    while (d->index > 0 && value <
           low + (int)((d->index * span + DURATION_STEPS - 1) / DURATION_STEPS) - h) d->index--;
    return DURATION_MIN_S + d->index * DURATION_STEP_S;
}
