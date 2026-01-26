/**
 * @file main.c
 * @brief IoT Crockpot main entry point
 *
 * Initializes all subsystems and starts FreeRTOS tasks.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "wifi.h"
#include "crockpot.h"
#include "telegram.h"
#include "display.h"

static const char* TAG = "main";

// Task stack sizes
#define CONTROL_TASK_STACK_SIZE   4096
#define TELEGRAM_TASK_STACK_SIZE  8192
#define DISPLAY_TASK_STACK_SIZE   4096

// Task priorities (higher number = higher priority)
#define CONTROL_TASK_PRIORITY     5
#define TELEGRAM_TASK_PRIORITY    3
#define DISPLAY_TASK_PRIORITY     4

void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "    IoT Crockpot Controller");
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Firmware version: 0.1.0");
    ESP_LOGI(TAG, "Starting initialization...");

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (!wifi_init()) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        // Continue without WiFi - local control still works
    } else {
        // Start WiFi connection
        if (!wifi_connect()) {
            ESP_LOGE(TAG, "Failed to start WiFi connection");
        }
    }

    // Initialize crockpot core
    ESP_LOGI(TAG, "Initializing crockpot core...");
    if (!crockpot_init()) {
        ESP_LOGE(TAG, "Crockpot initialization failed!");
        esp_restart();
    }

    // Initialize display (local interface)
    ESP_LOGI(TAG, "Initializing display...");
    if (!display_init()) {
        ESP_LOGW(TAG, "Display initialization failed - continuing without local UI");
    }

    // Initialize Telegram interface
    ESP_LOGI(TAG, "Initializing Telegram interface...");
    if (!telegram_init()) {
        ESP_LOGW(TAG, "Telegram initialization failed - continuing without remote control");
    }

    // Wait for WiFi connection (with timeout)
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    if (wifi_wait_connected(WIFI_CONNECT_TIMEOUT_MS)) {
        char ip_str[16];
        if (wifi_get_ip_string(ip_str, sizeof(ip_str))) {
            ESP_LOGI(TAG, "WiFi connected! IP: %s", ip_str);
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timed out - continuing in offline mode");
    }

    // Create FreeRTOS tasks
    ESP_LOGI(TAG, "Creating tasks...");

    // Control task - main state machine loop
    BaseType_t ret = xTaskCreate(
        crockpot_control_task,
        "control",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        esp_restart();
    }

    // Telegram task - remote control via Telegram bot
    ret = xTaskCreate(
        telegram_task,
        "telegram",
        TELEGRAM_TASK_STACK_SIZE,
        NULL,
        TELEGRAM_TASK_PRIORITY,
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create Telegram task");
    }

    // Display task - local UI
    ret = xTaskCreate(
        display_task,
        "display",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create display task");
    }

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "    Initialization complete!");
    ESP_LOGI(TAG, "=================================");

    // Main task can be deleted - all work is done in other tasks
    // Or we can use it for watchdog feeding and health monitoring
    while (1) {
        // Log periodic status
        crockpot_status_t status = crockpot_get_status();
        ESP_LOGI(TAG, "Status: %s | Temp: %.1f F | Uptime: %lu s | WiFi: %s",
                 crockpot_state_to_string(status.state),
                 status.temperature_f,
                 (unsigned long)status.uptime_seconds,
                 status.wifi_connected ? "OK" : "DISCONNECTED");

        vTaskDelay(pdMS_TO_TICKS(30000));  // Log every 30 seconds
    }
}
