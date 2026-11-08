#include "box_config.h"
#include "box_json.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

bool box_uuid_valid(const char *uuid)
{
    if (!uuid || strlen(uuid) != 36) return false;
    for (unsigned i = 0; i < 36; i++) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? uuid[i] != '-' : !isxdigit((unsigned char)uuid[i])) return false;
    }
    return true;
}
bool box_config_valid(const box_config_t *c)
{
    if (!c || c->version != BOX_CONFIG_VERSION) return false;
    if (!memchr(c->ssid, 0, sizeof(c->ssid)) || !memchr(c->password, 0, sizeof(c->password)) ||
        !memchr(c->base_url, 0, sizeof(c->base_url)) || !memchr(c->table_id, 0, sizeof(c->table_id)) ||
        !memchr(c->device_key, 0, sizeof(c->device_key))) return false;
    size_t pass_len = strlen(c->password);
    if (!c->ssid[0] || (pass_len && pass_len < 8) || !c->device_key[0] || !box_uuid_valid(c->table_id)) return false;
    if (pass_len == 64) for (size_t i = 0; i < pass_len; i++) if (!isxdigit((unsigned char)c->password[i])) return false;
    for (const char *p = c->device_key; *p; p++) if ((unsigned char)*p < 33 || (unsigned char)*p > 126) return false;
    const char *host;
    if (!strncmp(c->base_url, "https://", 8)) host = c->base_url + 8;
    else if (c->allow_http && !strncmp(c->base_url, "http://", 7)) host = c->base_url + 7;
    else return false;
    if (!*host || *host == '/' || *host == ':') return false;
    for (const char *p = host; *p; p++) {
        if (!(isalnum((unsigned char)*p) || strchr(".-:/[]_%~", *p))) return false;
    }
    char authority[193] = {0};
    size_t n = strcspn(host, "/");
    for (size_t i = 0; i < n; i++) authority[i] = (char)tolower((unsigned char)host[i]);
    if (!strcmp(authority, "localhost") || !strncmp(authority, "localhost:", 10) ||
        !strncmp(authority, "127.", 4) || !strncmp(authority, "[::1]", 5)) return false;
    return true;
}
static bool copy_field(const cJSON *json, const char *name, char *dest, size_t capacity)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsString(field) || strlen(field->valuestring) >= capacity) return false;
    strcpy(dest, field->valuestring);
    return true;
}
bool box_config_parse(const char *data, size_t length, box_config_t *out)
{
    if (!data || !out || !length || length > BOX_PROVISION_LINE_MAX || memchr(data, 0, length)) return false;
    // Caller provides a NUL at data[length]; reject embedded escaped NUL strings.
    if (data[length] != 0 || strstr(data, "\\u0000") || !box_json_safe(data, length)) return false;
    cJSON *json = cJSON_ParseWithLengthOpts(data, length + 1, NULL, true);
    if (!json) return false;
    bool ok = false;
    box_config_t c = {.version = BOX_CONFIG_VERSION};
    if (!cJSON_IsObject(json)) goto done;
    const cJSON *a, *b;
    cJSON_ArrayForEach(a, json) {
        for (b = a->next; b; b = b->next) if (!strcmp(a->string, b->string)) goto done;
    }
    if (!copy_field(json, "ssid", c.ssid, sizeof(c.ssid)) ||
        !copy_field(json, "password", c.password, sizeof(c.password)) ||
        !copy_field(json, "base_url", c.base_url, sizeof(c.base_url)) ||
        !copy_field(json, "table_id", c.table_id, sizeof(c.table_id)) ||
        !copy_field(json, "device_key", c.device_key, sizeof(c.device_key))) goto done;
    const cJSON *allow = cJSON_GetObjectItemCaseSensitive(json, "allow_http");
    if (allow && !cJSON_IsBool(allow)) goto done;
    c.allow_http = cJSON_IsTrue(allow);
    size_t size = strlen(c.base_url);
    while (size && c.base_url[size - 1] == '/') c.base_url[--size] = 0;
    for (char *p = c.table_id; *p; p++) *p = (char)tolower((unsigned char)*p);
    ok = box_config_valid(&c);
    if (ok) *out = c;
done:
    memset(&c, 0, sizeof(c));
    cJSON_Delete(json);
    return ok;
}
bool box_config_url(const box_config_t *c, char *url, size_t capacity)
{
    if (!box_config_valid(c)) return false;
    int n = snprintf(url, capacity, "%s/hw/tables/%s/state", c->base_url, c->table_id);
    return n >= 0 && (size_t)n < capacity;
}
bool box_config_link(const box_config_t *c, char *link, size_t capacity)
{
    if (!box_config_valid(c)) return false;
    int n = snprintf(link, capacity, "firstplace://table/%s", c->table_id);
    return n >= 0 && (size_t)n < capacity;
}
