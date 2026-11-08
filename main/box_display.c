#include "box_display.h"
#include "oled.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t screens;
#ifdef CONFIG_BOX_OLED_ENABLED
static void display_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t config = {.i2c_port = I2C_NUM_0,
        .sda_io_num = BOX_OLED_SDA, .scl_io_num = BOX_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    esp_err_t err = i2c_new_master_bus(&config, &bus);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = i2c_master_probe(bus, BOX_OLED_ADDRESS, 100);
    }
    if (err == ESP_OK) err = oled_init(bus, BOX_OLED_ADDRESS, OLED_HEIGHT);
    if (err != ESP_OK) {
        ESP_LOGW("box_oled", "OLED offline; light control continues");
        if (bus) i2c_del_master_bus(bus);
        vTaskDelete(NULL); return;
    }
    box_screen_t previous = {0}, current;
    bool first = true;
    for (;;) {
        if (xQueueReceive(screens, &current, portMAX_DELAY) != pdPASS) continue;
        if (!first && current.health == previous.health && current.light == previous.light &&
            current.provisioned == previous.provisioned) continue;
        oled_clear();
        oled_write_text(0, 0, "First Place");
        if (!current.provisioned) {
            oled_write_text(0, 2, "Setup required");
            oled_write_text(0, 4, "Provision over USB");
        } else if (current.health == BOX_FRESH) {
            char line[22];
            snprintf(line, sizeof(line), "Light: %s", box_light_name(current.light));
            oled_write_text(0, 2, line);
            oled_write_text(0, 4, "Backend controlled");
        } else {
            oled_write_text(0, 2, current.health == BOX_CONFIG_FAULT ? "Configuration fault" :
                                    current.health == BOX_NO_SIGNAL ? "NO SIGNAL" : "Waiting for server");
            oled_write_text(0, 4, "Conversation LEDs off");
        }
        if (oled_show() != ESP_OK) { ESP_LOGW("box_oled", "Display failed; lights continue"); vTaskDelete(NULL); return; }
        previous = current; first = false;
    }
}
#endif
esp_err_t box_display_start(void)
{
#ifdef CONFIG_BOX_OLED_ENABLED
    screens = xQueueCreate(1, sizeof(box_screen_t));
    if (!screens) return ESP_ERR_NO_MEM;
    if (xTaskCreate(display_task, "box_oled", 4096, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(screens); screens = NULL; return ESP_ERR_NO_MEM;
    }
#endif
    return ESP_OK;
}
void box_display_submit(const box_screen_t *screen) { if (screens) xQueueOverwrite(screens, screen); }
