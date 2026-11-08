#include "backend_adapter.h"
#include "beacon.h"
#include "beacon_ui.h"
#include "display.h"
#include "hardware.h"
#include "input_logic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // No camera driver, networking, stored sessions, or peripheral use beyond
    // the fixed assignments in beacon_config.h. Disconnect the camera ribbon.
    hardware_init();
    ESP_ERROR_CHECK(backend_adapter_init());
    beacon_t beacon;
    beacon_init(&beacon, backend_report_local, NULL);
    ESP_LOGI("beacon", "%s mode: %d-%d seconds, step %d; camera must be disconnected",
             BEACON_DEMO ? "DEMO" : "PRODUCTION", DURATION_MIN_S, DURATION_MAX_S, DURATION_STEP_S);
    esp_err_t err = display_start();
    if (err != ESP_OK) ESP_LOGW("beacon", "Display task unavailable: %s", esp_err_to_name(err));
    err = backend_start_mock_console();
    if (err != ESP_OK) ESP_LOGW("beacon", "Mock console unavailable: %s", esp_err_to_name(err));

    debounce_t buttons[BUTTON_COUNT] = {0};
    dial_t dial = {0};
    availability_t last_availability = AVAIL_RED;
    int64_t next_adc = 0, next_screen = 0, next_adc_log = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const int64_t now = esp_timer_get_time() / 1000;
        // Ordering: inbound invitations, expiration, idle dial, then press edges.
        // At an already-expired deadline Timer acknowledges, it does not cancel.
        char id[INVITE_ID_MAX + 1];
        for (unsigned n = 0; n < ADAPTER_QUEUE_LENGTH && backend_poll_invitation(id); n++) {
            if (beacon_receive_invitation(&beacon, id, now)) {
                ESP_LOGI("beacon", "Local invitation %s: %s", id[0] ? "pending" : "cleared", id);
            }
        }
        beacon_tick(&beacon, now);
        if (beacon.timer == TIMER_IDLE && now >= next_adc) {
            int raw;
            err = hardware_read_pot(&raw);
            next_adc = now + ADC_PERIOD_MS;
            if (err == ESP_OK) {
                unsigned before = beacon.selected_s;
                beacon_select_duration(&beacon, dial_update(&dial, raw));
                if (before != beacon.selected_s) ESP_LOGI("beacon", "Selected %u seconds", beacon.selected_s);
#ifdef CONFIG_THIRD_PLACE_ADC_LOG
                if (now >= next_adc_log) {
                    ESP_LOGI("dial", "raw=%d filtered=%d selected_s=%u", raw, dial.filtered_q8 / 256, beacon.selected_s);
                    next_adc_log = now + 1000;
                }
#endif
            } else if (now >= next_adc_log) {
                ESP_LOGW("dial", "Read failed: %s; keeping duration", esp_err_to_name(err));
                next_adc_log = now + 5000;
            }
        }
        // Deterministic simultaneous-press order: Accept, Skip, Status, Timer.
        for (button_t button = 0; button < BUTTON_COUNT; button++) {
            if (button_update(&buttons[button], hardware_button_pressed(button), now)) {
                beacon_press(&beacon, button, now);
                ESP_LOGI("button", "%d: %s", button + 1,
                         button == BUTTON_STATUS ? beacon_availability_text(beacon.availability) : beacon.feedback);
            }
        }
        if (last_availability != beacon.availability) {
            hardware_set_availability(beacon.availability);
            last_availability = beacon.availability;
        }
        hardware_set_buzzer(beacon_alarm_on(&beacon, now));
        if (now >= next_screen) {
            beacon_screen_t screen;
            beacon_format_screen(&beacon, now, &screen);
            display_submit(&screen);
            next_screen = now + DISPLAY_PERIOD_MS;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS) > 0 ? pdMS_TO_TICKS(CONTROL_PERIOD_MS) : 1);
    }
}
