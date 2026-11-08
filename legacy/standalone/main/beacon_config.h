#pragma once

// All physical assignments are ESP32 GPIO numbers, never header positions.
#define PIN_LED_RED 25
#define PIN_LED_YELLOW 26
#define PIN_LED_GREEN 27
#define PIN_ACCEPT 18
#define PIN_SKIP 19
#define PIN_STATUS 32
#define PIN_TIMER 33
#define PIN_POT 34
#define POT_ADC_UNIT ADC_UNIT_1
#define POT_ADC_CHANNEL ADC_CHANNEL_6
#define POT_ADC_ATTENUATION ADC_ATTEN_DB_12
#define PIN_BUZZER 13
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32
#error "Third Place requires the classic ESP32 / Freenove WROVER-CAM"
#endif
#endif

// Host tests use the same defaults; ESP-IDF overrides these via menuconfig.
#ifndef CONFIG_THIRD_PLACE_ADC_LOW
#define CONFIG_THIRD_PLACE_ADC_LOW 100
#endif
#ifndef CONFIG_THIRD_PLACE_ADC_HIGH
#define CONFIG_THIRD_PLACE_ADC_HIGH 3900
#endif
#ifndef CONFIG_THIRD_PLACE_ADC_HYSTERESIS
#define CONFIG_THIRD_PLACE_ADC_HYSTERESIS 20
#endif
#ifndef CONFIG_THIRD_PLACE_OLED_ADDRESS
#define CONFIG_THIRD_PLACE_OLED_ADDRESS 0x3C
#endif

#ifdef CONFIG_THIRD_PLACE_DEMO
#define BEACON_DEMO 1
#define DURATION_MIN_S 5
#define DURATION_STEP_S 5
#define DURATION_STEPS 12
#else
#define BEACON_DEMO 0
#define DURATION_MIN_S 60
#define DURATION_STEP_S 60
#define DURATION_STEPS 30
#endif
#define DURATION_MAX_S (DURATION_MIN_S + (DURATION_STEPS - 1) * DURATION_STEP_S)
#define CONTROL_PERIOD_MS 10
#define DEBOUNCE_MS 30
#define ADC_PERIOD_MS 20
#define ADC_SMOOTH_DIVISOR 8
#define DISPLAY_PERIOD_MS 200
#define FEEDBACK_MS 1500
#define BUZZER_HZ 2000
#define BUZZER_ON_MS 200
#define BUZZER_GAP_MS 150
#define BUZZER_BEEPS 3
#define INVITE_ID_MAX 64
#define ADAPTER_QUEUE_LENGTH 8
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_HZ 100000
// SSD1306 only. Other controllers require changing the panel factory in oled.c.
#define OLED_MIRROR_X true
#define OLED_MIRROR_Y true

_Static_assert(CONFIG_THIRD_PLACE_ADC_LOW >= 0 && CONFIG_THIRD_PLACE_ADC_HIGH <= 4095,
               "ADC endpoints must be 12-bit raw counts");
_Static_assert(CONFIG_THIRD_PLACE_ADC_LOW < CONFIG_THIRD_PLACE_ADC_HIGH,
               "ADC low endpoint must be below high endpoint");
_Static_assert(2 * CONFIG_THIRD_PLACE_ADC_HYSTERESIS * DURATION_STEPS <
               CONFIG_THIRD_PLACE_ADC_HIGH - CONFIG_THIRD_PLACE_ADC_LOW,
               "ADC hysteresis must be smaller than half a duration step");
