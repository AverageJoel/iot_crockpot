/**
 * @file interface_blynk.c
 * @brief Blynk IoT platform interface (STUB)
 *
 * Future implementation placeholder.
 * See: https://blynk.io/
 */

#include "interface_blynk.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "blynk";

bool blynk_init(void)
{
    ESP_LOGI(TAG, "Blynk interface not implemented");
    return false;
}

void blynk_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Blynk task started (stub)");

    // Stub - just suspend the task
    vTaskSuspend(NULL);
}

bool blynk_is_connected(void)
{
    return false;
}

bool blynk_set_token(const char* token)
{
    (void)token;
    ESP_LOGW(TAG, "Blynk not implemented");
    return false;
}
