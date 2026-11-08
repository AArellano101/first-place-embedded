#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "box_settings.h"
typedef struct {
    uint32_t version;
    char ssid[33], password[65], base_url[193], table_id[37], device_key[129];
    bool allow_http;
} box_config_t;
bool box_uuid_valid(const char *uuid);
bool box_config_valid(const box_config_t *config);
bool box_config_parse(const char *json, size_t length, box_config_t *config);
bool box_config_url(const box_config_t *config, char *url, size_t capacity);
bool box_config_link(const box_config_t *config, char *link, size_t capacity);
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t box_config_load(box_config_t *config);
esp_err_t box_config_save(const box_config_t *config);
esp_err_t box_config_erase(void);
esp_err_t box_provision_start(void);
#endif
