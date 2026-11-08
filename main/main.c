#include "box_config.h"
#include "box_display.h"
#include "box_led.h"
#include "box_network.h"
#include "box_controls.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static QueueHandle_t updates;
static bool provisioned;
static void light_task(void *arg)
{
    (void)arg;
    box_model_t model;
    box_model_init(&model, esp_timer_get_time() / 1000);
    TickType_t wake = xTaskGetTickCount();
    int64_t next_display = 0;
    box_timer_t timer = {.selected_minutes = 1};
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        box_update_t update;
        if (xQueueReceive(updates, &update, 0) == pdPASS) {
            box_model_update(&model, &update);
        }
        box_output_t output = box_model_output(&model, now, BOX_BRIGHTNESS, BOX_PULSE_MS);
        box_led_apply(&output);
        if (now >= next_display) {
            box_controls_snapshot(&timer);
            const box_screen_t screen = {.health = output.health, .light = output.light, .provisioned = provisioned,
                .timer_running = timer.running, .timer_finished = timer.finished,
                .timer_seconds = box_timer_remaining(&timer, now), .selected_minutes = timer.selected_minutes};
            box_display_submit(&screen);
            next_display = now + 200;
        }
        // No HTTP, OLED transactions, serial logging or other blocking I/O here.
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(BOX_CONTROL_MS));
    }
}
void app_main(void)
{
    // Local timer inputs never feed the API light model.
    ESP_ERROR_CHECK(box_led_init()); // All lamps dark before NVS/Wi-Fi initialization.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // Do not silently erase provisioned credentials on NVS corruption/version errors.
        ESP_LOGE("table_box", "NVS unavailable; lamps off. Recover storage over USB.");
        return;
    }
    box_config_t config = {0};
    provisioned = box_config_load(&config) == ESP_OK;
    updates = xQueueCreate(1, sizeof(box_update_t));
    ESP_ERROR_CHECK(updates ? ESP_OK : ESP_ERR_NO_MEM);
    err = box_display_start();
    if (err != ESP_OK) ESP_LOGW("table_box", "Display task unavailable");
    err = box_controls_start();
    if (err != ESP_OK) ESP_LOGW("table_box", "Local timer inputs unavailable");
    ESP_ERROR_CHECK(xTaskCreate(light_task, "light_safety", 3072, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(box_provision_start());
    if (provisioned) {
        ESP_LOGI("table_box", "Provisioned; API controls lamps, local timer only. Camera must be disconnected.");
        if (config.allow_http) ESP_LOGW("table_box", "Development HTTP permitted; use HTTPS for deployment");
        err = box_network_start(&config, updates);
        if (err != ESP_OK) ESP_LOGE("table_box", "Network initialization failed; lamps remain fail-closed");
    } else ESP_LOGI("table_box", "Unprovisioned: use tools/provision.py; conversation lamps stay off");
    // Wi-Fi task has its own immutable copy. No credentials in logs or source.
    memset(&config, 0, sizeof(config));
}
