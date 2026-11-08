#pragma once
#include "beacon.h"
#include "esp_err.h"

// Initialize once before any producers. No networking/credentials in this adapter.
esp_err_t backend_adapter_init(void);
// Task-safe, nonblocking inbound hook. Empty string clears the current invitation.
// Copies the ID; returns INVALID_ARG or TIMEOUT (queue full), never silently drops.
esp_err_t backend_receive_invitation(const char *id);
esp_err_t backend_clear_invitation(void);
// Control-loop consumer only; payloads are already copied/validated.
bool backend_poll_invitation(char id[INVITE_ID_MAX + 1]);
// Local event sink, NOT backend confirmation. Future adapters must enqueue work.
void backend_report_local(const beacon_event_t *event, void *context);
esp_err_t backend_start_mock_console(void);
