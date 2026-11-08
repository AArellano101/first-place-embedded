#include "display.h"
#include "oled.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t screens;

static void display_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_OLED_SDA, .scl_io_num = PIN_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&config, &bus);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100)); // OLED power-up; never blocks controls.
        err = i2c_master_probe(bus, CONFIG_THIRD_PLACE_OLED_ADDRESS, 100);
    }
    if (err == ESP_OK) err = oled_init(bus, CONFIG_THIRD_PLACE_OLED_ADDRESS, OLED_HEIGHT);
    if (err != ESP_OK) {
        ESP_LOGW("display", "OLED offline (%s); controls/timer remain active", esp_err_to_name(err));
        if (bus) i2c_del_master_bus(bus);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI("display", "Assuming SSD1306 128x64 at 0x%02X", CONFIG_THIRD_PLACE_OLED_ADDRESS);
    beacon_screen_t current, previous = {0};
    bool first = true;
    for (;;) {
        if (xQueueReceive(screens, &current, portMAX_DELAY) != pdPASS) continue;
        if (!first && memcmp(&current, &previous, sizeof(current)) == 0) continue;
        oled_clear();
        for (unsigned row = 0; row < 8; row++) oled_write_text(0, row, current.lines[row]);
        err = oled_show();
        if (err != ESP_OK) {
            ESP_LOGW("display", "OLED write failed (%s); display disabled until reboot", esp_err_to_name(err));
            // Stop retrying a broken bus. Only this task can touch panel resources.
            vTaskDelete(NULL);
            return;
        }
        previous = current;
        first = false;
    }
}

esp_err_t display_start(void)
{
    screens = xQueueCreate(1, sizeof(beacon_screen_t));
    if (!screens) return ESP_ERR_NO_MEM;
    if (xTaskCreate(display_task, "oled", 4096, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(screens);
        screens = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void display_submit(const beacon_screen_t *screen)
{
    if (screens) xQueueOverwrite(screens, screen);
}
