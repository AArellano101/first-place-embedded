#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "beacon_config.h"

typedef enum { AVAIL_RED, AVAIL_YELLOW, AVAIL_GREEN } availability_t;
typedef enum { TIMER_IDLE, TIMER_RUNNING, TIMER_EXPIRED } beacon_timer_t;
typedef enum { BUTTON_ACCEPT, BUTTON_SKIP, BUTTON_STATUS, BUTTON_TIMER, BUTTON_COUNT } button_t;
typedef enum {
    EVENT_AVAILABILITY, EVENT_ACCEPT, EVENT_SKIP,
    EVENT_TIMER_START, EVENT_TIMER_CANCEL, EVENT_TIMER_EXPIRE
} beacon_event_type_t;
typedef struct {
    beacon_event_type_t type;
    availability_t availability;
    unsigned duration_s;
    char invitation_id[INVITE_ID_MAX + 1];
} beacon_event_t;
// Synchronous callback: must be fast/nonblocking and copy events before return.
typedef void (*beacon_report_fn)(const beacon_event_t *event, void *context);
typedef struct {
    availability_t availability;
    beacon_timer_t timer;
    unsigned selected_s;
    unsigned latched_s;
    int64_t deadline_ms;
    int64_t alarm_started_ms;
    char invitation_id[INVITE_ID_MAX + 1];
    char feedback[22];
    int64_t feedback_until_ms;
    beacon_report_fn report;
    void *report_context;
} beacon_t;

void beacon_init(beacon_t *b, beacon_report_fn report, void *context);
void beacon_select_duration(beacon_t *b, unsigned seconds);
void beacon_press(beacon_t *b, button_t button, int64_t now_ms);
void beacon_tick(beacon_t *b, int64_t now_ms);
unsigned beacon_remaining_s(const beacon_t *b, int64_t now_ms);
bool beacon_alarm_on(const beacon_t *b, int64_t now_ms);
// Empty ID clears; invalid/oversize IDs are rejected without changing state.
bool beacon_receive_invitation(beacon_t *b, const char *id, int64_t now_ms);
const char *beacon_availability_text(availability_t availability);
