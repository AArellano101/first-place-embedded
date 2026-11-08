#pragma once
#include <stddef.h>
#include "box_model.h"
typedef struct { char data[BOX_RESPONSE_MAX + 1]; size_t length; bool rejected, finished; } box_response_t;
void box_response_reset(box_response_t *response);
bool box_response_append(box_response_t *response, const void *data, size_t length);
bool box_response_parse(const box_response_t *response, bool transport_ok, int status,
                        bool complete, box_light_t *light);
bool box_http_config_fault(int status);
