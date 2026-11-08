#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "beacon.h"
#include "beacon_ui.h"
#include "input_logic.h"

static unsigned events[6];
static beacon_event_t last_event;
static void record(const beacon_event_t *event, void *context)
{
    (void)context;
    events[event->type]++;
    last_event = *event;
}
static beacon_t fresh(void)
{
    memset(events, 0, sizeof(events));
    beacon_t b;
    beacon_init(&b, record, NULL);
    return b;
}

static void test_availability_and_invites(void)
{
    beacon_t b = fresh();
    assert(b.availability == AVAIL_RED && b.timer == TIMER_IDLE);
    assert(!beacon_alarm_on(&b, 0) && !b.invitation_id[0]);
    assert(events[EVENT_AVAILABILITY] == 1);
    beacon_press(&b, BUTTON_ACCEPT, 1);
    assert(b.availability == AVAIL_RED && events[EVENT_ACCEPT] == 0);
    beacon_press(&b, BUTTON_STATUS, 2);
    assert(b.availability == AVAIL_YELLOW);
    beacon_press(&b, BUTTON_ACCEPT, 3);
    assert(b.availability == AVAIL_GREEN && b.timer == TIMER_IDLE);
    assert(events[EVENT_ACCEPT] == 0);
    beacon_press(&b, BUTTON_STATUS, 4);
    assert(b.availability == AVAIL_RED);
    beacon_press(&b, BUTTON_STATUS, 5);
    beacon_press(&b, BUTTON_STATUS, 6);
    assert(b.availability == AVAIL_GREEN);
    beacon_press(&b, BUTTON_SKIP, 7);
    assert(b.availability == AVAIL_GREEN && events[EVENT_SKIP] == 0);
    assert(strcmp(b.feedback, "No pending invite") == 0 && !beacon_alarm_on(&b, 7));

    assert(beacon_receive_invitation(&b, "uuid-123", 8));
    beacon_press(&b, BUTTON_ACCEPT, 9);
    assert(events[EVENT_ACCEPT] == 1 && strcmp(last_event.invitation_id, "uuid-123") == 0);
    assert(!b.invitation_id[0] && b.timer == TIMER_IDLE);
    beacon_press(&b, BUTTON_ACCEPT, 10);
    assert(events[EVENT_ACCEPT] == 1);
    assert(beacon_receive_invitation(&b, "skip-me", 11));
    beacon_press(&b, BUTTON_TIMER, 12);
    int64_t deadline = b.deadline_ms;
    beacon_press(&b, BUTTON_SKIP, 13);
    assert(events[EVENT_SKIP] == 1 && strcmp(last_event.invitation_id, "skip-me") == 0);
    assert(b.availability == AVAIL_GREEN && b.timer == TIMER_RUNNING && b.deadline_ms == deadline);
    assert(!beacon_alarm_on(&b, 13) && !b.invitation_id[0]);

    assert(beacon_receive_invitation(&b, "old", 14));
    assert(beacon_receive_invitation(&b, "replacement", 15));
    assert(strcmp(b.invitation_id, "replacement") == 0);
    char too_long[INVITE_ID_MAX + 2];
    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1] = 0;
    assert(!beacon_receive_invitation(&b, too_long, 16));
    assert(!beacon_receive_invitation(&b, "has space", 16));
    assert(!beacon_receive_invitation(&b, NULL, 16));
    assert(strcmp(b.invitation_id, "replacement") == 0);
    too_long[INVITE_ID_MAX] = 0;
    assert(beacon_receive_invitation(&b, too_long, 17));
    assert(strlen(b.invitation_id) == INVITE_ID_MAX);
    assert(beacon_receive_invitation(&b, "", 18) && !b.invitation_id[0]);
}

static void test_timer_and_alarm(void)
{
    beacon_t b = fresh();
    // Beyond 32-bit milliseconds, as can happen after long uptime.
    const int64_t start = INT64_C(5000000000);
    beacon_select_duration(&b, DURATION_MAX_S);
    beacon_press(&b, BUTTON_TIMER, start);
    assert(events[EVENT_TIMER_START] == 1 && b.latched_s == DURATION_MAX_S);
    beacon_select_duration(&b, DURATION_MIN_S);
    assert(b.selected_s == DURATION_MAX_S && b.latched_s == DURATION_MAX_S);
    int64_t deadline = b.deadline_ms;
    beacon_press(&b, BUTTON_STATUS, start + 10);
    assert(b.deadline_ms == deadline && b.availability == AVAIL_YELLOW);
    assert(beacon_remaining_s(&b, start + 1) == DURATION_MAX_S);
    assert(beacon_remaining_s(&b, deadline - 1) == 1);
    beacon_press(&b, BUTTON_TIMER, start + 20);
    assert(b.timer == TIMER_IDLE && events[EVENT_TIMER_CANCEL] == 1);
    beacon_tick(&b, deadline + 1);
    assert(events[EVENT_TIMER_EXPIRE] == 0 && !beacon_alarm_on(&b, deadline + 1));

    beacon_select_duration(&b, DURATION_MIN_S);
    beacon_select_duration(&b, 0); // invalid selections must not corrupt the setting
    assert(b.selected_s == DURATION_MIN_S);
    beacon_press(&b, BUTTON_TIMER, deadline + 10);
    const int64_t end = b.deadline_ms;
    beacon_tick(&b, end - 1);
    assert(b.timer == TIMER_RUNNING);
    beacon_tick(&b, end);
    assert(b.timer == TIMER_EXPIRED && b.availability == AVAIL_YELLOW);
    assert(events[EVENT_TIMER_EXPIRE] == 1);
    unsigned rising_edges = 0, on_ms = 0;
    bool previous = false;
    for (int t = 0; t < 2000; t++) {
        beacon_tick(&b, end + t);
        bool on = beacon_alarm_on(&b, end + t);
        if (on && !previous) rising_edges++;
        if (on) on_ms++;
        previous = on;
    }
    assert(rising_edges == 3 && on_ms == 600 && !previous);
    assert(!beacon_alarm_on(&b, end + 200));
    assert(beacon_alarm_on(&b, end + 350));
    assert(!beacon_alarm_on(&b, end + 550));
    assert(beacon_alarm_on(&b, end + 700));
    assert(!beacon_alarm_on(&b, end + 900));
    assert(events[EVENT_TIMER_EXPIRE] == 1);
    beacon_press(&b, BUTTON_TIMER, end + 2001);
    assert(b.timer == TIMER_IDLE); // Acknowledge only; no automatic restart.
    beacon_press(&b, BUTTON_TIMER, end + 2100);
    assert(b.timer == TIMER_RUNNING);
    beacon_tick(&b, b.deadline_ms + 100); // delayed loop still expires once
    int64_t alarm = b.alarm_started_ms;
    beacon_press(&b, BUTTON_STATUS, alarm + 20);
    assert(b.availability == AVAIL_GREEN && beacon_alarm_on(&b, alarm + 20));
    beacon_press(&b, BUTTON_TIMER, alarm + 50);
    assert(b.timer == TIMER_IDLE && !beacon_alarm_on(&b, alarm + 50));
    beacon_tick(&b, alarm + 10000);
    assert(events[EVENT_TIMER_EXPIRE] == 2);
    beacon_init(&b, record, NULL);
    assert(b.timer == TIMER_IDLE && b.availability == AVAIL_RED && !beacon_alarm_on(&b, alarm + 10001));
}

static void test_debounce(void)
{
    debounce_t d = {0};
    assert(!button_update(&d, true, 0));
    assert(!button_update(&d, false, 10));
    assert(!button_update(&d, true, 15));
    assert(!button_update(&d, true, 44));
    assert(button_update(&d, true, 45));
    for (int t = 46; t < 500; t++) assert(!button_update(&d, true, t));
    assert(!button_update(&d, false, 500));
    assert(!button_update(&d, true, 510)); // release bounce is not a fresh press
    assert(!button_update(&d, false, 520));
    assert(!button_update(&d, false, 550));
    assert(!button_update(&d, true, 600));
    assert(button_update(&d, true, 630));
}

static unsigned settle(dial_t *d, int raw)
{
    unsigned value = 0;
    for (unsigned i = 0; i < 200; i++) value = dial_update(d, raw);
    return value;
}
static void test_dial(void)
{
    dial_t d = {0};
    assert(dial_update(&d, -100) == DURATION_MIN_S);
    unsigned previous = DURATION_MIN_S;
    for (int raw = 0; raw <= 4095; raw += 7) {
        unsigned value = settle(&d, raw);
        assert(value >= previous && value <= DURATION_MAX_S);
        assert((value - DURATION_MIN_S) % DURATION_STEP_S == 0);
        previous = value;
    }
    assert(settle(&d, 5000) == DURATION_MAX_S);
    assert(settle(&d, -100) == DURATION_MIN_S);
    const int boundary = CONFIG_THIRD_PLACE_ADC_LOW +
        (CONFIG_THIRD_PLACE_ADC_HIGH - CONFIG_THIRD_PLACE_ADC_LOW + 1) / DURATION_STEPS;
    assert(settle(&d, boundary - 40) == DURATION_MIN_S);
    for (int i = 0; i < 400; i++) assert(dial_update(&d, boundary + (i % 2 ? 2 : -2)) == DURATION_MIN_S);
    assert(settle(&d, boundary + 45) == DURATION_MIN_S + DURATION_STEP_S);
    for (int i = 0; i < 400; i++) assert(dial_update(&d, boundary + (i % 2 ? 2 : -2)) == DURATION_MIN_S + DURATION_STEP_S);
}

static void test_screen(void)
{
    beacon_t b = fresh();
    beacon_screen_t screen;
    beacon_format_screen(&b, 0, &screen);
    assert(strcmp(screen.lines[1], "RED") == 0);
    assert((strstr(screen.lines[0], "DEMO") != NULL) == BEACON_DEMO);
    beacon_receive_invitation(&b, "id", 1);
    beacon_format_screen(&b, 2, &screen);
    assert(strcmp(screen.lines[7], "Invite: pending") == 0);
    beacon_press(&b, BUTTON_TIMER, 10);
    beacon_format_screen(&b, b.deadline_ms - 1234, &screen);
    assert(strcmp(screen.lines[4], "Left: 00:02") == 0);
    beacon_tick(&b, b.deadline_ms);
    beacon_format_screen(&b, b.deadline_ms, &screen);
    assert(strcmp(screen.lines[4], "Time's up.") == 0);
    assert(!screen.lines[6][0]); // Temporary feedback expires without blocking.
    for (unsigned i = 0; i < 8; i++) assert(strlen(screen.lines[i]) <= 21);
}

int main(void)
{
    test_availability_and_invites();
    test_timer_and_alarm();
    test_debounce();
    test_dial();
    test_screen();
    printf("PASS: %s availability/invitations, timer/alarm, debounce, dial, OLED text\n", BEACON_DEMO ? "demo" : "production");
    return 0;
}
