#include "box_led.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const int pins[] = {BOX_LED_RED, BOX_LED_YELLOW, BOX_LED_GREEN};
static unsigned previous[3] = {0};
static bool status_previous;
static bool usable_pin(int pin)
{
    // Exclude memory, strapping, serial console, input-only pins and old buzzer.
    const int allowed[] = {4,14,18,19,21,22,23,25,26,27,32,33};
    for (unsigned i = 0; i < sizeof(allowed) / sizeof(*allowed); i++) if (pin == allowed[i]) return true;
    return false;
}
esp_err_t box_led_init(void)
{
    // Validate before touching any configurable output.
    for (unsigned i = 0; i < 3; i++) {
        if (!usable_pin(pins[i])) return ESP_ERR_INVALID_ARG;
        for (unsigned j = 0; j < i; j++) if (pins[i] == pins[j]) return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_BOX_OLED_ENABLED
        if (pins[i] == BOX_OLED_SDA || pins[i] == BOX_OLED_SCL) return ESP_ERR_INVALID_ARG;
#endif
        if (pins[i] == BOX_LED_STATUS) return ESP_ERR_INVALID_ARG;
    }
    if (BOX_LED_STATUS >= 0 && !usable_pin(BOX_LED_STATUS)) return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_BOX_OLED_ENABLED
    if (!usable_pin(BOX_OLED_SDA) || !usable_pin(BOX_OLED_SCL) || BOX_OLED_SDA == BOX_OLED_SCL ||
        BOX_LED_STATUS == BOX_OLED_SDA || BOX_LED_STATUS == BOX_OLED_SCL) return ESP_ERR_INVALID_ARG;
#endif
    // Silence the previously connected active-high buzzer; it has no role now.
    ESP_ERROR_CHECK(gpio_set_level(BOX_INACTIVE_BUZZER_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_direction(BOX_INACTIVE_BUZZER_GPIO, GPIO_MODE_OUTPUT));
    const ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0, .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    for (unsigned i = 0; i < 3; i++) {
        const ledc_channel_config_t channel = {.gpio_num = pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = (ledc_channel_t)i,
            .timer_sel = LEDC_TIMER_0, .duty = 0,
            .flags.output_invert = BOX_LED_ACTIVE_LOW};
        err = ledc_channel_config(&channel);
        if (err != ESP_OK) return err;
    }
    if (BOX_LED_STATUS >= 0) {
        ESP_ERROR_CHECK(gpio_set_level(BOX_LED_STATUS, 0));
        ESP_ERROR_CHECK(gpio_set_direction(BOX_LED_STATUS, GPIO_MODE_OUTPUT));
    }
    return ESP_OK;
}
static void set_duty(unsigned channel, unsigned duty)
{
    if (previous[channel] == duty) return;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel));
    previous[channel] = duty;
}
void box_led_apply(const box_output_t *o)
{
#ifdef CONFIG_BOX_ANALOG_RGB
    // Channel order follows configurable pins: red, blue (formerly yellow), green.
    const unsigned duties[] = {o->red + o->yellow, 0, o->green + o->yellow};
#else
    const unsigned duties[] = {o->red, o->yellow, o->green};
#endif
    // Break before make, without clearing/restarting the yellow PWM every tick.
    for (unsigned i = 0; i < 3; i++) if (!duties[i]) set_duty(i, 0);
    for (unsigned i = 0; i < 3; i++) if (duties[i]) set_duty(i, duties[i]);
    if (BOX_LED_STATUS >= 0 && o->status_on != status_previous) {
        ESP_ERROR_CHECK(gpio_set_level(BOX_LED_STATUS, o->status_on));
        status_previous = o->status_on;
    }
}
