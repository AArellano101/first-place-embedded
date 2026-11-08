#include "beacon.h"
#include <string.h>

static void report(beacon_t *b, beacon_event_type_t type)
{
    beacon_event_t event = {
        .type = type, .availability = b->availability, .duration_s = b->latched_s,
    };
    if (type == EVENT_ACCEPT || type == EVENT_SKIP) {
        memcpy(event.invitation_id, b->invitation_id, sizeof(event.invitation_id));
    }
    if (b->report) b->report(&event, b->report_context);
}

static void feedback(beacon_t *b, const char *text, int64_t now)
{
    // All local feedback strings fit one 21-character display row.
    strncpy(b->feedback, text, sizeof(b->feedback) - 1);
    b->feedback[sizeof(b->feedback) - 1] = '\0';
    b->feedback_until_ms = now + FEEDBACK_MS;
}

const char *beacon_availability_text(availability_t availability)
{
    switch (availability) {
    case AVAIL_RED: return "Do not disturb";
    case AVAIL_YELLOW: return "Tentative";
    case AVAIL_GREEN: return "Open to talk";
    default: return "Unknown";
    }
}

void beacon_init(beacon_t *b, beacon_report_fn callback, void *context)
{
    *b = (beacon_t){.availability = AVAIL_RED, .timer = TIMER_IDLE,
        .selected_s = DURATION_MIN_S, .report = callback, .report_context = context};
    report(b, EVENT_AVAILABILITY);
}

void beacon_select_duration(beacon_t *b, unsigned seconds)
{
    if (b->timer == TIMER_IDLE && seconds >= DURATION_MIN_S && seconds <= DURATION_MAX_S &&
        (seconds - DURATION_MIN_S) % DURATION_STEP_S == 0) b->selected_s = seconds;
}

bool beacon_receive_invitation(beacon_t *b, const char *id, int64_t now)
{
    if (!id) return false;
    size_t length = 0;
    while (length <= INVITE_ID_MAX && id[length]) {
        // IDs are opaque printable ASCII tokens, e.g. UUIDs; never truncated.
        if ((unsigned char)id[length] <= 32 || (unsigned char)id[length] > 126) return false;
        length++;
    }
    if (length > INVITE_ID_MAX) return false;
    memcpy(b->invitation_id, id, length + 1);
    feedback(b, length ? "Invite pending" : "Invite cleared", now);
    return true;
}

void beacon_press(beacon_t *b, button_t button, int64_t now)
{
    switch (button) {
    case BUTTON_STATUS:
        b->availability = (b->availability + 1) % 3;
        report(b, EVENT_AVAILABILITY);
        break;
    case BUTTON_ACCEPT:
        if (b->availability == AVAIL_YELLOW) {
            b->availability = AVAIL_GREEN;
            report(b, EVENT_AVAILABILITY);
        }
        if (b->invitation_id[0]) {
            report(b, EVENT_ACCEPT);
            b->invitation_id[0] = '\0';
            feedback(b, "Accepted locally", now);
        } else {
            feedback(b, "No pending invite", now);
        }
        break;
    case BUTTON_SKIP:
        if (b->invitation_id[0]) {
            report(b, EVENT_SKIP);
            b->invitation_id[0] = '\0';
            feedback(b, "Skipped locally", now);
        } else feedback(b, "No pending invite", now);
        break;
    case BUTTON_TIMER:
        if (b->timer == TIMER_IDLE) {
            b->latched_s = b->selected_s;
            b->deadline_ms = now + (int64_t)b->latched_s * 1000;
            b->timer = TIMER_RUNNING;
            report(b, EVENT_TIMER_START);
            feedback(b, "Timer started", now);
        } else if (b->timer == TIMER_RUNNING) {
            b->timer = TIMER_IDLE;
            report(b, EVENT_TIMER_CANCEL);
            feedback(b, "Timer cancelled", now);
        } else {
            b->timer = TIMER_IDLE; // Also silences the alarm immediately.
            feedback(b, "Timer acknowledged", now);
        }
        break;
    default: break;
    }
}

void beacon_tick(beacon_t *b, int64_t now)
{
    if (b->timer == TIMER_RUNNING && now >= b->deadline_ms) {
        b->timer = TIMER_EXPIRED;
        b->alarm_started_ms = now;
        report(b, EVENT_TIMER_EXPIRE);
    }
}

unsigned beacon_remaining_s(const beacon_t *b, int64_t now)
{
    if (b->timer != TIMER_RUNNING || now >= b->deadline_ms) return 0;
    return (unsigned)((b->deadline_ms - now + 999) / 1000);
}

bool beacon_alarm_on(const beacon_t *b, int64_t now)
{
    int64_t elapsed = now - b->alarm_started_ms;
    const int period = BUZZER_ON_MS + BUZZER_GAP_MS;
    const int total = BUZZER_BEEPS * BUZZER_ON_MS + (BUZZER_BEEPS - 1) * BUZZER_GAP_MS;
    return b->timer == TIMER_EXPIRED && elapsed >= 0 && elapsed < total &&
           elapsed % period < BUZZER_ON_MS;
}
