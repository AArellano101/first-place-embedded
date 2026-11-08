#include "beacon_ui.h"
#include <stdio.h>
#include <string.h>

void beacon_format_screen(const beacon_t *b, int64_t now, beacon_screen_t *s)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->lines[0], sizeof(s->lines[0]), "Third Place%s", BEACON_DEMO ? " DEMO" : "");
    const char *colors[] = {"RED", "YELLOW", "GREEN"};
    snprintf(s->lines[1], sizeof(s->lines[1]), "%s", colors[b->availability]);
    snprintf(s->lines[2], sizeof(s->lines[2]), "%s", beacon_availability_text(b->availability));
    if (b->timer == TIMER_IDLE) {
        snprintf(s->lines[4], sizeof(s->lines[4]), "Set: %02u:%02u", b->selected_s / 60, b->selected_s % 60);
        snprintf(s->lines[5], sizeof(s->lines[5]), "Timer: press to start");
    } else if (b->timer == TIMER_RUNNING) {
        unsigned remaining = beacon_remaining_s(b, now);
        snprintf(s->lines[4], sizeof(s->lines[4]), "Left: %02u:%02u", remaining / 60, remaining % 60);
        snprintf(s->lines[5], sizeof(s->lines[5]), "Timer: cancel");
    } else {
        snprintf(s->lines[4], sizeof(s->lines[4]), "Time's up.");
        snprintf(s->lines[5], sizeof(s->lines[5]), "Timer: acknowledge");
    }
    if (now < b->feedback_until_ms) snprintf(s->lines[6], sizeof(s->lines[6]), "%s", b->feedback);
    snprintf(s->lines[7], sizeof(s->lines[7]), "Invite: %s", b->invitation_id[0] ? "pending" : "none");
}
