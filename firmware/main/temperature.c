/**
 * @file temperature.c
 * @brief Temperature sensor driver implementation
 *
 * TODO: Implement concrete sensor driver based on selected hardware.
 * Current implementation is a stub that returns simulated values.
 */

#include "temperature.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "temperature";

// Sensor initialized flag
static bool s_initialized = false;

// Simulated temperature for testing (remove when real sensor implemented)
static float s_simulated_temp_c = 25.0f;

bool temperature_init(void)
{
    ESP_LOGI(TAG, "Initializing temperature sensor on GPIO %d", TEMPERATURE_SENSOR_GPIO);

    // TODO: Initialize actual sensor (DS18B20, thermocouple, etc.)
    // For now, just configure the GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TEMPERATURE_SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Temperature sensor initialized (STUB - implement real driver)");
    return true;
}

temperature_reading_t temperature_read(void)
{
    temperature_reading_t reading = {
        .temperature_f = 0.0f,
        .temperature_c = 0.0f,
        .valid = false
    };

    if (!s_initialized) {
        ESP_LOGW(TAG, "Sensor not initialized");
        return reading;
    }

    // TODO: Implement actual sensor reading
    // For now, return simulated temperature
    reading.temperature_c = s_simulated_temp_c;
    reading.temperature_f = temperature_c_to_f(reading.temperature_c);
    reading.valid = true;

    ESP_LOGD(TAG, "Temperature: %.1f C (%.1f F) [SIMULATED]",
             reading.temperature_c, reading.temperature_f);

    return reading;
}

float temperature_c_to_f(float celsius)
{
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

float temperature_f_to_c(float fahrenheit)
{
    return (fahrenheit - 32.0f) * 5.0f / 9.0f;
}

bool temperature_sensor_ok(void)
{
    // TODO: Implement actual sensor health check
    return s_initialized;
}
