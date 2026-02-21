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
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_task_wdt.h"

#include "wifi.h"
#include "crockpot.h"
#include "telegram.h"
#include "display.h"
#include "gui.h"

static const char* TAG = "main";

// Task stack sizes
#define CONTROL_TASK_STACK_SIZE   4096
#define TELEGRAM_TASK_STACK_SIZE  8192
#define DISPLAY_TASK_STACK_SIZE   4096

// Task priorities (higher number = higher priority)
#define CONTROL_TASK_PRIORITY     5
#define TELEGRAM_TASK_PRIORITY    3
#define DISPLAY_TASK_PRIORITY     4

// Alert callback — fired by crockpot_control_task on safety shutoff
static void on_safety_alert(crockpot_alert_t reason, float temp_f)
{
    char msg[128];
    if (reason == CROCKPOT_ALERT_TEMP_LIMIT) {
        snprintf(msg, sizeof(msg),
                 "SAFETY SHUTOFF: Temperature %.1f F exceeded limit. Crockpot turned OFF.", temp_f);
    } else {
        snprintf(msg, sizeof(msg),
                 "SAFETY SHUTOFF: Persistent sensor error. Crockpot turned OFF.");
    }
    ESP_LOGW(TAG, "%s", msg);
    telegram_queue_alert(msg);
    gui_show_error(msg);
}

void app_main(void)
{
    // --- Boot log ---
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "    IoT Crockpot Controller");
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "Firmware:  0.1.0");
    ESP_LOGI(TAG, "IDF:       %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip:      %s  cores=%d  rev=%d",
             CONFIG_IDF_TARGET, chip.cores, chip.revision);
    ESP_LOGI(TAG, "Flash:     %d MB  %s",
             (chip.features & CHIP_FEATURE_EMB_FLASH) ? 4 : 0,
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "");
    ESP_LOGI(TAG, "SPI:       SCK=%d MOSI=%d MISO=%d",
             CONFIG_CROCKPOT_SPI_SCK, CONFIG_CROCKPOT_SPI_MOSI, CONFIG_CROCKPOT_SPI_MISO);
    ESP_LOGI(TAG, "Display:   CS=%d DC=%d RST=%d BL=%d",
             CONFIG_CROCKPOT_LCD_CS, CONFIG_CROCKPOT_LCD_DC,
             CONFIG_CROCKPOT_LCD_RST, CONFIG_CROCKPOT_LCD_BL);
    ESP_LOGI(TAG, "Touch:     SDA=%d SCL=%d RST=%d INT=%d",
             CONFIG_CROCKPOT_TOUCH_SDA, CONFIG_CROCKPOT_TOUCH_SCL,
             CONFIG_CROCKPOT_TOUCH_RST, CONFIG_CROCKPOT_TOUCH_INT);
    ESP_LOGI(TAG, "TC CS:     GPIO %d", CONFIG_CROCKPOT_TC_CS);
    ESP_LOGI(TAG, "Relay:     GPIO %d  active-%s",
             CONFIG_CROCKPOT_RELAY_GPIO,
             CONFIG_CROCKPOT_RELAY_ACTIVE_HIGH ? "high" : "low");
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
    crockpot_set_alert_callback(on_safety_alert);

    // Initialize display hardware + LVGL
    ESP_LOGI(TAG, "Initializing display...");
    if (!display_init()) {
        ESP_LOGW(TAG, "Display initialization failed - continuing without local UI");
    } else {
        // Build LVGL screen objects and start the status update timer
        if (!gui_init()) {
            ESP_LOGW(TAG, "GUI init failed");
        } else {
            gui_start();
        }
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
