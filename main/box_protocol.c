#include "box_protocol.h"
#include "box_json.h"
#include <string.h>
#include <ctype.h>
#include "cJSON.h"

void box_response_reset(box_response_t *r) { *r = (box_response_t){0}; }
bool box_response_append(box_response_t *r, const void *data, size_t length)
{
    if (r->rejected) return false;
    if (length > BOX_RESPONSE_MAX - r->length || (length && (!data || memchr(data, 0, length)))) {
        r->rejected = true;
        return false;
    }
    if (length) memcpy(r->data + r->length, data, length);
    r->length += length;
    r->data[r->length] = 0;
    return true;
}
bool box_http_config_fault(int status)
{
    return status == 400 || status == 401 || status == 403 || status == 404 ||
           (status >= 300 && status <= 399);
}
bool box_response_parse(const box_response_t *r, bool transport_ok, int status, bool complete, box_light_t *out)
{
    if (!out || !transport_ok || status != 200 || !complete || !r->finished || r->rejected || !r->length) return false;
    // cJSON strings cannot expose embedded NUL lengths: reject their escaped form too.
    if (strstr(r->data, "\\u0000") || !box_json_safe(r->data, r->length)) return false;
    cJSON *json = cJSON_ParseWithLengthOpts(r->data, r->length + 1, NULL, true);
    if (!json) return false;
    bool valid = false;
    const cJSON *light = NULL, *at = NULL, *poll = NULL, *field = NULL;
    if (!cJSON_IsObject(json)) goto done;
    cJSON_ArrayForEach(field, json) {
        if (!strcmp(field->string, "light")) { if (light) goto done; light = field; }
        if (!strcmp(field->string, "at")) { if (at) goto done; at = field; }
        if (!strcmp(field->string, "pollMs")) { if (poll) goto done; poll = field; }
    }
    if (!cJSON_IsString(light) || !cJSON_IsString(at) || !cJSON_IsNumber(poll) || poll->valuedouble != BOX_POLL_MS) goto done;
    // Current server emits Date.toISOString(); this is metadata, never a freshness clock.
    const char *timestamp = at->valuestring;
    if (strlen(timestamp) != 24) goto done;
    for (unsigned i = 0; i < 24; i++) {
        char expected = i == 4 || i == 7 ? '-' : i == 10 ? 'T' : i == 13 || i == 16 ? ':' : i == 19 ? '.' : i == 23 ? 'Z' : 0;
        if (expected ? timestamp[i] != expected : !isdigit((unsigned char)timestamp[i])) goto done;
    }
    for (box_light_t color = LIGHT_OFF; color <= LIGHT_RED; color++) {
        if (!strcmp(light->valuestring, box_light_name(color))) { *out = color; valid = true; break; }
    }
done:
    cJSON_Delete(json);
    return valid;
}
