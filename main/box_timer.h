#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned selected_minutes;
    bool running, finished;
    int64_t deadline_ms;
} box_timer_t;
void box_timer_step(box_timer_t *timer, int64_t now_ms, unsigned minutes,
                    bool start_pressed, bool stop_pressed);
unsigned box_timer_remaining(const box_timer_t *timer, int64_t now_ms);
