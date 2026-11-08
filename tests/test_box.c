#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "box_config.h"
#include "box_model.h"
#include "box_protocol.h"
#include "cJSON.h"

static size_t live_allocations;
static void *tracked_malloc(size_t size) { void *p = malloc(size); if (p) live_allocations++; return p; }
static void tracked_free(void *p) { if (p) { assert(live_allocations > 0); live_allocations--; } free(p); }
static const char *payload = "{\"light\":\"yellow\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}";
static bool parse(const char *text, int status, bool complete)
{
    box_response_t r;
    box_response_reset(&r);
    box_response_append(&r, text, strlen(text));
    r.finished = true;
    box_light_t light = LIGHT_OFF;
    return box_response_parse(&r, true, status, complete, &light);
}
static void protocol_tests(void)
{
    for (unsigned fragment = 1; fragment <= 19; fragment++) {
        box_response_t r;
        box_response_reset(&r);
        size_t length = strlen(payload);
        for (size_t pos = 0; pos < length; pos += fragment) {
            size_t n = length - pos < fragment ? length - pos : fragment;
            assert(box_response_append(&r, payload + pos, n));
        }
        box_light_t color;
        assert(!box_response_parse(&r, true, 200, true, &color));
        r.finished = true;
        assert(box_response_parse(&r, true, 200, true, &color) && color == LIGHT_YELLOW);
        assert(!box_response_parse(&r, false, 200, true, &color));
        assert(!box_response_parse(&r, true, 200, false, &color));
    }
    const char *bad[] = {
        "", "{", "null", "[]", "{\"lit\":true}", "{\"openToChat\":true}",
        "{\"light\":\"welcome\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}",
        "{\"light\":\"full\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}",
        "{\"light\":true,\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}",
        "{\"light\":\"green\",\"light\":\"red\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}",
        "{\"light\":\"green\\u0000evil\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}",
        "{\"light\":\"green\",\"at\":\"bad timestamp\",\"pollMs\":2000}",
        "{\"light\":\"green\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":999999}",
        "{\"light\":\"green\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":02000}",
        "{\"light\":\"green\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000.}",
        "{\"light\":\"green\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000,\"extra\":\"raw\nnewline\"}",
        "{\"light\":\"green\",\"at\":\"2026-11-08T12:00:00.000Z\"}",
        "{\"light\":\"green\"}garbage", "[[[[[[[[[0]]]]]]]]]"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) assert(!parse(bad[i], 200, true));
    int statuses[] = {301, 400, 401, 403, 404, 500, 503, 204};
    for (unsigned i = 0; i < sizeof(statuses) / sizeof(*statuses); i++) assert(!parse(payload, statuses[i], true));
    assert(box_http_config_fault(401) && box_http_config_fault(400) && box_http_config_fault(404));
    assert(box_http_config_fault(302) && !box_http_config_fault(500) && !box_http_config_fault(200));
    box_response_t r;
    box_response_reset(&r);
    char full[BOX_RESPONSE_MAX + 1]; memset(full, ' ', sizeof(full));
    memcpy(full, payload, strlen(payload));
    assert(box_response_append(&r, full, BOX_RESPONSE_MAX));
    r.finished = true;
    box_light_t light;
    assert(box_response_parse(&r, true, 200, true, &light));
    assert(!box_response_append(&r, " ", 1));
    assert(!box_response_parse(&r, true, 200, true, &light));
    box_response_reset(&r);
    assert(!box_response_append(&r, "a\0b", 3));
    for (box_light_t color = LIGHT_OFF; color <= LIGHT_RED; color++) {
        char json[128];
        snprintf(json, sizeof(json), "{\"light\":\"%s\",\"at\":\"2026-11-08T12:00:00.000Z\",\"pollMs\":2000}", box_light_name(color));
        assert(parse(json, 200, true));
    }
}
static bool dark(box_output_t o) { return !o.red && !o.yellow && !o.green; }
static void model_tests(void)
{
    const int64_t now = INT64_C(9000000000);
    box_model_t m;
    box_model_init(&m, now);
    assert(dark(box_model_output(&m, now, 100, 2000)));
    assert(box_model_output(&m, now, 100, 2000).health == BOX_WAITING);
    assert(box_model_output(&m, now + 10000, 100, 2000).health == BOX_NO_SIGNAL);
    for (box_light_t color = LIGHT_OFF; color <= LIGHT_RED; color++) {
        box_update_t update = {.valid = true, .light = color, .received_ms = now + 10001 + color};
        box_model_update(&m, &update);
        box_output_t o = box_model_output(&m, update.received_ms, 100, 2000);
        assert(o.health == BOX_FRESH && o.light == color);
        assert((o.red > 0) == (color == LIGHT_RED));
        assert((o.green > 0) == (color == LIGHT_GREEN));
        assert((o.yellow > 0) == (color == LIGHT_YELLOW));
    }
    box_update_t yellow = {.valid = true, .light = LIGHT_YELLOW, .received_ms = now + 20000};
    box_model_update(&m, &yellow);
    assert(box_model_output(&m, yellow.received_ms - 1, 100, 2000).yellow <= 1023);
    unsigned low = box_model_output(&m, yellow.received_ms, 100, 2000).yellow;
    unsigned peak = box_model_output(&m, yellow.received_ms + 1000, 100, 2000).yellow;
    assert(low > 0 && peak == 1023 && peak > low);
    int64_t phase = m.pulse_started_ms;
    yellow.received_ms += 600;
    box_model_update(&m, &yellow);
    assert(m.pulse_started_ms == phase);
    assert(box_model_output(&m, phase + 1000, 100, 2000).yellow == peak);
    int64_t last_good = m.last_valid_ms;
    box_update_t failed = {.valid = false, .received_ms = last_good + 8000};
    box_model_update(&m, &failed);
    assert(m.last_valid_ms == last_good);
    assert(box_model_output(&m, last_good + 9000, 100, 2000).health == BOX_FRESH);
    // No network event at all models a stalled HTTP task or Wi-Fi loss.
    assert(dark(box_model_output(&m, last_good + 10000, 100, 2000)));
    assert(box_model_output(&m, last_good + 10000, 100, 2000).health == BOX_NO_SIGNAL);
    yellow.received_ms = last_good + 11000;
    box_model_update(&m, &yellow);
    assert(box_model_output(&m, yellow.received_ms, 100, 2000).health == BOX_FRESH);
    box_update_t fault = {.config_fault = true};
    box_model_update(&m, &fault);
    assert(dark(box_model_output(&m, yellow.received_ms, 100, 2000)));
    assert(box_model_output(&m, yellow.received_ms, 100, 2000).health == BOX_CONFIG_FAULT);
    yellow.received_ms++;
    yellow.light = LIGHT_OFF; // Includes unknown-UUID server response: ordinary valid off.
    box_model_update(&m, &yellow);
    assert(box_model_output(&m, yellow.received_ms, 100, 2000).health == BOX_FRESH);
    assert(dark(box_model_output(&m, yellow.received_ms, 100, 2000)));
    box_model_init(&m, now);
    assert(!m.ever_valid && dark(box_model_output(&m, now, 100, 2000)));
    for (unsigned failures = 0; failures < 100; failures++) {
        unsigned delay = box_retry_delay(failures, 12345);
        assert(delay >= 2000 && delay <= 30000);
    }
    assert(box_retry_delay(0, 123) == 2000);
}
static const char *config_json = "{\"ssid\":\"test-wifi\",\"password\":\"test-only-password\",\"base_url\":\"http://192.0.2.1:8787/\",\"table_id\":\"12345678-1234-5678-9ABC-123456789ABC\",\"device_key\":\"test-key\",\"allow_http\":true}";
static void config_tests(void)
{
    box_config_t c;
    assert(box_config_parse(config_json, strlen(config_json), &c));
    assert(box_config_valid(&c));
    char url[300], link[100];
    assert(box_config_url(&c, url, sizeof(url)));
    assert(!strcmp(url, "http://192.0.2.1:8787/hw/tables/12345678-1234-5678-9abc-123456789abc/state"));
    assert(box_config_link(&c, link, sizeof(link)));
    assert(!strcmp(link, "firstplace://table/12345678-1234-5678-9abc-123456789abc"));
    assert(!box_config_url(&c, url, 10));
    assert(!box_uuid_valid("seed-table-1"));
    c.allow_http = false; assert(!box_config_valid(&c));
    strcpy(c.base_url, "https://example.com"); assert(box_config_valid(&c));
    strcpy(c.base_url, "https://user:secret@example.com"); assert(!box_config_valid(&c));
    strcpy(c.base_url, "https://localhost:8787"); assert(!box_config_valid(&c));
    strcpy(c.base_url, "https://example.com/?key=secret"); assert(!box_config_valid(&c));
    strcpy(c.base_url, "https://example.com");
    strcpy(c.device_key, "bad\r\nheader"); assert(!box_config_valid(&c));
    assert(!box_config_parse("{}", 2, &c));
}
int main(void)
{
    cJSON_Hooks hooks = {.malloc_fn = tracked_malloc, .free_fn = tracked_free};
    cJSON_InitHooks(&hooks);
    protocol_tests(); model_tests(); config_tests();
    assert(live_allocations == 0);
    for (unsigned i = 0; i < 10000; i++) {
        assert(parse(payload, 200, true));
        assert(!parse("{bad", 200, true));
        box_config_t c;
        assert(box_config_parse(config_json, strlen(config_json), &c));
        assert(live_allocations == 0);
    }
    puts("PASS: protocol/fragments/errors, four lights/pulse/freshness/recovery, provisioning/identity, 10000 parse cycles without live allocation growth");
    return 0;
}
