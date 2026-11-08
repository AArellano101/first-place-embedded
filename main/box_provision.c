#include "box_config.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

esp_err_t box_config_load(box_config_t *c)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("table_box", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t length = sizeof(*c);
    err = nvs_get_blob(handle, "provision", c, &length);
    nvs_close(handle);
    if (err == ESP_OK && (length != sizeof(*c) || !box_config_valid(c))) err = ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) memset(c, 0, sizeof(*c));
    return err;
}
esp_err_t box_config_save(const box_config_t *c)
{
    if (!box_config_valid(c)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("table_box", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "provision", c, sizeof(*c));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
esp_err_t box_config_erase(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("table_box", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
static void reply(const char *text)
{
    uart_write_bytes(UART_NUM_0, text, strlen(text));
    uart_write_bytes(UART_NUM_0, "\r\n", 2);
}
static void command(char *line)
{
    box_config_t config = {0};
    if (!strcmp(line, "INFO")) {
        if (box_config_load(&config) == ESP_OK) {
            char message[128];
            snprintf(message, sizeof(message), "BOX_INFO {\"table_id\":\"%s\",\"provisioned\":true}", config.table_id);
            reply(message); // No SSID, password, URL or device key is returned.
        } else reply("BOX_INFO {\"provisioned\":false}");
    } else if (!strcmp(line, "ERASE")) {
        if (box_config_erase() == ESP_OK) {
            reply("BOX_ERASE_OK");
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        } else reply("BOX_ERROR storage");
    } else if (!strncmp(line, "PROVISION ", 10)) {
        if (!box_config_parse(line + 10, strlen(line + 10), &config)) reply("BOX_ERROR invalid_config");
        else if (box_config_save(&config) != ESP_OK) reply("BOX_ERROR storage");
        else {
            reply("BOX_PROVISION_OK");
            memset(&config, 0, sizeof(config));
            memset(line, 0, strlen(line));
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
    } else if (*line) reply("BOX_ERROR unknown_command");
    memset(&config, 0, sizeof(config));
}
static void provision_task(void *arg)
{
    (void)arg;
    char line[BOX_PROVISION_LINE_MAX + 1] = {0};
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(1000)) != 1) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            if (overflow) reply("BOX_ERROR line_too_long");
            else { line[used] = 0; command(line); }
            memset(line, 0, sizeof(line));
            used = 0;
            overflow = false;
        } else if (used < BOX_PROVISION_LINE_MAX && c != 0 && !overflow) line[used++] = (char)c;
        else overflow = true;
        // Intentionally no echo, history, or raw input logging.
    }
}
esp_err_t box_provision_start(void)
{
    const uart_config_t config = {.baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_param_config(UART_NUM_0, &config);
    if (err != ESP_OK) return err;
    err = uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    return xTaskCreate(provision_task, "provision", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
