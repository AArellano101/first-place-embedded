#include "box_timer.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    box_timer_t t = {0};
    box_timer_step(&t, 0, 5, false, false);
    assert(!t.running && box_timer_remaining(&t, 0) == 300);
    box_timer_step(&t, 100, 5, true, false);
    assert(t.running && box_timer_remaining(&t, 101) == 300);
    box_timer_step(&t, 1100, 30, true, false);
    assert(t.deadline_ms == 300100 && box_timer_remaining(&t, 1100) == 299);
    box_timer_step(&t, 300100, 30, false, false);
    assert(!t.running && t.finished && box_timer_remaining(&t, 300100) == 0);
    box_timer_step(&t, 400000, 2, true, false);
    assert(t.running && !t.finished && t.deadline_ms == 520000);
    box_timer_step(&t, 401000, 3, true, true);
    assert(!t.running && !t.finished && box_timer_remaining(&t, 401000) == 180);
    box_timer_step(&t, 402000, 0, true, false);
    assert(t.deadline_ms == 462000);
    box_timer_step(&t, 403000, 100, false, true);
    assert(t.selected_minutes == 30);
    puts("Local timer tests passed");
}
