#include "box_controls.h"
#include "box_settings.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t snapshots;
static adc_oneshot_unit_handle_t adc;
typedef struct { bool raw, stable; int64_t changed; } button_t;
static bool pressed(button_t *b, int pin, int64_t now)
{
    bool down = gpio_get_level(pin) == 0;
    if (down != b->raw) { b->raw = down; b->changed = now; }
    if (b->stable != b->raw && now - b->changed >= 40) {
        b->stable = b->raw;
        return b->stable;
    }
    return false;
}
static void controls_task(void *arg)
{
    (void)arg;
    box_timer_t timer = {.selected_minutes = 1};
    button_t start = {0}, stop = {0};
    int filtered = -1;
    unsigned minutes = 1;
    int64_t next_sample = 0;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now >= next_sample) {
            int raw;
            if (adc_oneshot_read(adc, ADC_CHANNEL_6, &raw) == ESP_OK) {
                filtered = filtered < 0 ? raw : (filtered * 3 + raw) / 4;
                // Equal-width bins with hysteresis to avoid flickering at a boundary.
                int candidate = filtered * 30 / 4096 + 1;
                if (candidate > 30) candidate = 30;
                if (candidate > (int)minutes && filtered > (int)minutes * 4096 / 30 + 20)
                    minutes = (unsigned)candidate;
                if (candidate < (int)minutes && filtered < ((int)minutes - 1) * 4096 / 30 - 20)
                    minutes = (unsigned)candidate;
            }
            next_sample = now + 100;
        }
        bool start_edge = pressed(&start, BOX_START_GPIO, now);
        bool stop_edge = pressed(&stop, BOX_STOP_GPIO, now);
        box_timer_step(&timer, now, minutes, start_edge, stop_edge || stop.stable);
        xQueueOverwrite(snapshots, &timer);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t box_controls_start(void)
{
    const gpio_config_t buttons = {.pin_bit_mask = (1ULL << BOX_START_GPIO) | (1ULL << BOX_STOP_GPIO),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_DISABLE};
    esp_err_t err = gpio_config(&buttons);
    if (err != ESP_OK) return err;
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    err = adc_oneshot_new_unit(&unit, &adc);
    if (err != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    err = adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &channel); // GPIO34, ADC1 works with Wi-Fi.
    if (err != ESP_OK) { adc_oneshot_del_unit(adc); return err; }
    snapshots = xQueueCreate(1, sizeof(box_timer_t));
    if (!snapshots) { adc_oneshot_del_unit(adc); return ESP_ERR_NO_MEM; }
    if (xTaskCreate(controls_task, "local_timer", 3072, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(snapshots); snapshots = NULL; adc_oneshot_del_unit(adc); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void box_controls_snapshot(box_timer_t *timer)
{
    if (snapshots) xQueuePeek(snapshots, timer, 0);
}
