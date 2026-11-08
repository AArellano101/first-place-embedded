#include "box_model.h"

void box_model_init(box_model_t *m, int64_t now)
{
    *m = (box_model_t){.light = LIGHT_OFF, .boot_ms = now};
}
void box_model_update(box_model_t *m, const box_update_t *u)
{
    if (u->config_fault) { m->config_fault = true; return; }
    if (!u->valid || u->light < LIGHT_OFF || u->light > LIGHT_RED) return;
    if (m->ever_valid && u->received_ms < m->last_valid_ms) return;
    if (!m->ever_valid || m->light != u->light) m->pulse_started_ms = u->received_ms;
    m->light = u->light;
    m->last_valid_ms = u->received_ms;
    m->ever_valid = true;
    m->config_fault = false;
}
box_output_t box_model_output(const box_model_t *m, int64_t now, unsigned brightness, unsigned pulse_ms)
{
    box_output_t o = {.light = LIGHT_OFF};
    int64_t age = now - (m->ever_valid ? m->last_valid_ms : m->boot_ms);
    // A conservative control-period margin prevents a full extra tick of stale light.
    if (m->config_fault) o.health = BOX_CONFIG_FAULT;
    else if (age >= BOX_STALE_MS - BOX_CONTROL_MS) o.health = BOX_NO_SIGNAL;
    else if (!m->ever_valid) o.health = BOX_WAITING;
    else o.health = BOX_FRESH;
    if (o.health != BOX_FRESH) {
        o.status_on = (o.health == BOX_NO_SIGNAL || o.health == BOX_CONFIG_FAULT) && (now / 500) % 2 == 0;
        return o;
    }
    o.light = m->light;
    if (brightness > 100) brightness = 100;
    unsigned duty = 1023 * brightness / 100;
    if (m->light == LIGHT_RED) o.red = duty;
    if (m->light == LIGHT_GREEN) o.green = duty;
    if (m->light == LIGHT_YELLOW) {
        if (pulse_ms < 2) pulse_ms = 2000;
        // A response can arrive between a control tick's clock read and queue read.
        int64_t elapsed = now >= m->pulse_started_ms ? now - m->pulse_started_ms : 0;
        unsigned phase = (unsigned)(elapsed % pulse_ms);
        unsigned triangle = phase <= pulse_ms / 2 ? phase * 2000 / pulse_ms : (pulse_ms - phase) * 2000 / pulse_ms;
        // Smoothstep, 10%-100% brightness; no floats, same phase across equal responses.
        unsigned eased = (unsigned)((uint64_t)triangle * triangle * (3000 - 2 * triangle) / 1000000);
        o.yellow = duty * (100 + 900 * eased / 1000) / 1000;
    }
    return o;
}
unsigned box_retry_delay(unsigned failures, unsigned random_value)
{
    if (!failures) return BOX_POLL_MS;
    unsigned shift = failures > 5 ? 4 : failures - 1;
    unsigned delay = BOX_POLL_MS << shift;
    if (delay > BOX_RETRY_MAX_MS - 500) delay = BOX_RETRY_MAX_MS - 500;
    return delay + random_value % 501;
}
const char *box_light_name(box_light_t light)
{
    static const char *names[] = {"off", "green", "yellow", "red"};
    return light >= LIGHT_OFF && light <= LIGHT_RED ? names[light] : "invalid";
}
