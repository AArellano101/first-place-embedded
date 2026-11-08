#include "box_timer.h"

void box_timer_step(box_timer_t *t, int64_t now, unsigned minutes, bool start, bool stop)
{
    t->selected_minutes = minutes < 1 ? 1 : minutes > 30 ? 30 : minutes;
    if (stop) {
        t->running = false;
        t->finished = false;
    } else if (start && !t->running) {
        t->deadline_ms = now + (int64_t)t->selected_minutes * 60000;
        t->running = true;
        t->finished = false;
    }
    if (t->running && now >= t->deadline_ms) {
        t->running = false;
        t->finished = true;
    }
}
unsigned box_timer_remaining(const box_timer_t *t, int64_t now)
{
    if (!t->running) return t->finished ? 0 : t->selected_minutes * 60;
    return now >= t->deadline_ms ? 0 : (unsigned)((t->deadline_ms - now + 999) / 1000);
}
