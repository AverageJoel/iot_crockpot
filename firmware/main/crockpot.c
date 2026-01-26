/**
 * @file crockpot.c
 * @brief Core crockpot state machine implementation
 */

#include "crockpot.h"
#include "temperature.h"
#include "relay.h"
#include "wifi.h"

#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "crockpot";

// State protection mutex
static SemaphoreHandle_t s_state_mutex = NULL;

// Current state
static crockpot_status_t s_status = {
    .state = CROCKPOT_OFF,
    .temperature_f = 0.0f,
    .uptime_seconds = 0,
    .wifi_connected = false,
    .sensor_error = false
};

// Boot timestamp for uptime calculation
static int64_t s_boot_time_us = 0;

bool crockpot_init(void)
{
    ESP_LOGI(TAG, "Initializing crockpot control system");

    // Create state mutex
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }

    // Initialize temperature sensor
    if (!temperature_init()) {
        ESP_LOGE(TAG, "Failed to initialize temperature sensor");
        // Continue anyway - sensor might work later
    }

    // Initialize relay control
    if (!relay_init()) {
        ESP_LOGE(TAG, "Failed to initialize relay control");
        return false;
    }

    // Ensure we start in OFF state
    relay_all_off();

    // Record boot time
    s_boot_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Crockpot control system initialized");
    return true;
}

crockpot_status_t crockpot_get_status(void)
{
    crockpot_status_t status;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = s_status;
        xSemaphoreGive(s_state_mutex);
    } else {
        // Return last known state on timeout
        status = s_status;
    }

    return status;
}

bool crockpot_set_state(crockpot_state_t state)
{
    ESP_LOGI(TAG, "Setting state to: %s", crockpot_state_to_string(state));

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire state mutex");
        return false;
    }

    // Apply state to relay
    if (!relay_apply_state(state)) {
        ESP_LOGE(TAG, "Failed to apply state to relay");
        xSemaphoreGive(s_state_mutex);
        return false;
    }

    s_status.state = state;
    xSemaphoreGive(s_state_mutex);

    ESP_LOGI(TAG, "State changed to: %s", crockpot_state_to_string(state));
    return true;
}

const char* crockpot_state_to_string(crockpot_state_t state)
{
    switch (state) {
        case CROCKPOT_OFF:  return "OFF";
        case CROCKPOT_WARM: return "WARM";
        case CROCKPOT_LOW:  return "LOW";
        case CROCKPOT_HIGH: return "HIGH";
        default:            return "UNKNOWN";
    }
}

bool crockpot_state_from_string(const char* str, crockpot_state_t* out)
{
    if (str == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(str, "off") == 0) {
        *out = CROCKPOT_OFF;
        return true;
    }
    if (strcasecmp(str, "warm") == 0) {
        *out = CROCKPOT_WARM;
        return true;
    }
    if (strcasecmp(str, "low") == 0) {
        *out = CROCKPOT_LOW;
        return true;
    }
    if (strcasecmp(str, "high") == 0) {
        *out = CROCKPOT_HIGH;
        return true;
    }

    return false;
}

void crockpot_control_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Control task started");

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        // Read temperature
        temperature_reading_t reading = temperature_read();

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Update temperature
            if (reading.valid) {
                s_status.temperature_f = reading.temperature_f;
                s_status.sensor_error = false;
            } else {
                s_status.sensor_error = true;
            }

            // Update uptime
            int64_t now_us = esp_timer_get_time();
            s_status.uptime_seconds = (uint32_t)((now_us - s_boot_time_us) / 1000000);

            // Update WiFi status
            s_status.wifi_connected = wifi_is_connected();

            // Safety check: auto-shutoff on high temperature
            if (reading.valid && reading.temperature_f > CROCKPOT_SAFETY_TEMP_F) {
                ESP_LOGW(TAG, "SAFETY: Temperature %.1f F exceeds limit, shutting off",
                         reading.temperature_f);
                s_status.state = CROCKPOT_OFF;
                relay_all_off();
            }

            // Safety check: shut off on persistent sensor error while heating
            if (s_status.sensor_error && s_status.state != CROCKPOT_OFF) {
                static int error_count = 0;
                error_count++;
                if (error_count > 10) {  // 10 consecutive errors
                    ESP_LOGW(TAG, "SAFETY: Persistent sensor error, shutting off");
                    s_status.state = CROCKPOT_OFF;
                    relay_all_off();
                    error_count = 0;
                }
            }

            xSemaphoreGive(s_state_mutex);
        }

        // Wait for next cycle
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CROCKPOT_CONTROL_INTERVAL_MS));
    }
}
