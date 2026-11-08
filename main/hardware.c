#include "hardware.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const int button_pins[BUTTON_COUNT] = {PIN_ACCEPT, PIN_SKIP, PIN_STATUS, PIN_TIMER};
static const int led_pins[] = {PIN_LED_RED, PIN_LED_YELLOW, PIN_LED_GREEN};
static adc_oneshot_unit_handle_t adc;
static bool buzzer_on;

void hardware_set_availability(availability_t availability)
{
    // Break before make: no two status LEDs are ever driven high together.
    for (unsigned i = 0; i < 3; i++) ESP_ERROR_CHECK(gpio_set_level(led_pins[i], 0));
    ESP_ERROR_CHECK(gpio_set_level(led_pins[availability], 1));
}

void hardware_init(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_LED_RED) | (1ULL << PIN_LED_YELLOW) |
                        (1ULL << PIN_LED_GREEN) | (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BUZZER, 0));
    hardware_set_availability(AVAIL_RED);
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << PIN_ACCEPT) | (1ULL << PIN_SKIP) |
                        (1ULL << PIN_STATUS) | (1ULL << PIN_TIMER),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));
#ifndef CONFIG_THIRD_PLACE_ACTIVE_BUZZER
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = BUZZER_HZ, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t channel = {
        .gpio_num = PIN_BUZZER, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
#endif
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = POT_ADC_UNIT};
    esp_err_t err = adc_oneshot_new_unit(&unit, &adc);
    if (err == ESP_OK) {
        const adc_oneshot_chan_cfg_t channel = {.atten = POT_ADC_ATTENUATION, .bitwidth = ADC_BITWIDTH_12};
        err = adc_oneshot_config_channel(adc, POT_ADC_CHANNEL, &channel);
    }
    if (err != ESP_OK) {
        ESP_LOGE("hardware", "ADC unavailable (%s); keeping default duration", esp_err_to_name(err));
        if (adc) adc_oneshot_del_unit(adc);
        adc = NULL;
    }
}

bool hardware_button_pressed(button_t button) { return gpio_get_level(button_pins[button]) == 0; }
esp_err_t hardware_read_pot(int *raw)
{
    return adc ? adc_oneshot_read(adc, POT_ADC_CHANNEL, raw) : ESP_ERR_INVALID_STATE;
}

void hardware_set_buzzer(bool on)
{
    if (on == buzzer_on) return;
#ifdef CONFIG_THIRD_PLACE_ACTIVE_BUZZER
    ESP_ERROR_CHECK(gpio_set_level(PIN_BUZZER, on));
#else
    if (on) {
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    } else ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
#endif
    buzzer_on = on;
}
