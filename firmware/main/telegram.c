/**
 * @file telegram.c
 * @brief Telegram bot interface implementation
 */

#include "telegram.h"
#include "crockpot.h"
#include "wifi.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "cJSON.h"

static const char* TAG = "telegram";

// Bot token (loaded from NVS or hardcoded for development)
static char s_bot_token[64] = "";

// Last update ID for long polling
static int64_t s_last_update_id = 0;

// HTTP response buffer
static char s_response_buffer[TELEGRAM_MAX_MESSAGE_LEN];
static int s_response_len = 0;

// Connection status
static bool s_connected = false;

// Telegram API base URL
#define TELEGRAM_API_BASE "https://api.telegram.org/bot"

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_response_len + evt->data_len < sizeof(s_response_buffer) - 1) {
                memcpy(s_response_buffer + s_response_len, evt->data, evt->data_len);
                s_response_len += evt->data_len;
                s_response_buffer[s_response_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Build status message
static void build_status_message(char* buf, size_t buf_len)
{
    crockpot_status_t status = crockpot_get_status();

    snprintf(buf, buf_len,
        "Crockpot Status:\n"
        "State: %s\n"
        "Temperature: %.1f F\n"
        "Uptime: %lu seconds\n"
        "WiFi: %s\n"
        "Sensor: %s",
        crockpot_state_to_string(status.state),
        status.temperature_f,
        (unsigned long)status.uptime_seconds,
        status.wifi_connected ? "Connected" : "Disconnected",
        status.sensor_error ? "ERROR" : "OK"
    );
}

// Build help message
static void build_help_message(char* buf, size_t buf_len)
{
    snprintf(buf, buf_len,
        "IoT Crockpot Commands:\n"
        "/status - Show current status\n"
        "/off - Turn off\n"
        "/warm - Set to warm\n"
        "/low - Set to low\n"
        "/high - Set to high\n"
        "/help - Show this help"
    );
}

// Process a command and return response
static void process_command(const char* command, int64_t chat_id)
{
    char response[512];

    ESP_LOGI(TAG, "Processing command: %s", command);

    if (strcmp(command, "/status") == 0 || strcmp(command, "/start") == 0) {
        build_status_message(response, sizeof(response));
    }
    else if (strcmp(command, "/off") == 0) {
        if (crockpot_set_state(CROCKPOT_OFF)) {
            snprintf(response, sizeof(response), "Crockpot turned OFF");
        } else {
            snprintf(response, sizeof(response), "Failed to turn off crockpot");
        }
    }
    else if (strcmp(command, "/warm") == 0) {
        if (crockpot_set_state(CROCKPOT_WARM)) {
            snprintf(response, sizeof(response), "Crockpot set to WARM");
        } else {
            snprintf(response, sizeof(response), "Failed to set crockpot to warm");
        }
    }
    else if (strcmp(command, "/low") == 0) {
        if (crockpot_set_state(CROCKPOT_LOW)) {
            snprintf(response, sizeof(response), "Crockpot set to LOW");
        } else {
            snprintf(response, sizeof(response), "Failed to set crockpot to low");
        }
    }
    else if (strcmp(command, "/high") == 0) {
        if (crockpot_set_state(CROCKPOT_HIGH)) {
            snprintf(response, sizeof(response), "Crockpot set to HIGH");
        } else {
            snprintf(response, sizeof(response), "Failed to set crockpot to high");
        }
    }
    else if (strcmp(command, "/help") == 0) {
        build_help_message(response, sizeof(response));
    }
    else {
        snprintf(response, sizeof(response),
            "Unknown command: %s\nType /help for available commands.", command);
    }

    telegram_send_message(chat_id, response);
}

// Process updates from Telegram
static void process_updates(const char* json_response)
{
    cJSON* root = cJSON_Parse(json_response);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return;
    }

    cJSON* ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsTrue(ok)) {
        ESP_LOGE(TAG, "Telegram API error");
        cJSON_Delete(root);
        return;
    }

    cJSON* result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    cJSON* update;
    cJSON_ArrayForEach(update, result) {
        // Get update ID
        cJSON* update_id = cJSON_GetObjectItem(update, "update_id");
        if (cJSON_IsNumber(update_id)) {
            s_last_update_id = (int64_t)update_id->valuedouble + 1;
        }

        // Get message
        cJSON* message = cJSON_GetObjectItem(update, "message");
        if (message == NULL) {
            continue;
        }

        // Get chat ID
        cJSON* chat = cJSON_GetObjectItem(message, "chat");
        cJSON* chat_id = cJSON_GetObjectItem(chat, "id");
        if (!cJSON_IsNumber(chat_id)) {
            continue;
        }
        int64_t chat_id_val = (int64_t)chat_id->valuedouble;

        // Get text
        cJSON* text = cJSON_GetObjectItem(message, "text");
        if (cJSON_IsString(text) && text->valuestring != NULL) {
            // Check if it's a command (starts with /)
            if (text->valuestring[0] == '/') {
                // Extract command (remove @botname if present)
                char command[64];
                strncpy(command, text->valuestring, sizeof(command) - 1);
                command[sizeof(command) - 1] = '\0';

                char* at_sign = strchr(command, '@');
                if (at_sign) {
                    *at_sign = '\0';
                }

                process_command(command, chat_id_val);
            }
        }
    }

    cJSON_Delete(root);
}

bool telegram_init(void)
{
    ESP_LOGI(TAG, "Initializing Telegram interface");

    // TODO: Load token from NVS
    // For now, check if token is set
    if (strlen(s_bot_token) == 0) {
        ESP_LOGW(TAG, "Telegram bot token not configured");
        ESP_LOGW(TAG, "Set token using telegram_set_token() or configure in NVS");
        return false;
    }

    ESP_LOGI(TAG, "Telegram interface initialized");
    return true;
}

void telegram_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Telegram task started");

    // Wait for WiFi connection
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Check if token is configured
    if (strlen(s_bot_token) == 0) {
        ESP_LOGW(TAG, "Bot token not configured, Telegram task suspended");
        vTaskSuspend(NULL);
    }

    char url[256];

    while (1) {
        // Wait for WiFi if disconnected
        if (!wifi_is_connected()) {
            s_connected = false;
            vTaskDelay(pdMS_TO_TICKS(TELEGRAM_RETRY_INTERVAL_MS));
            continue;
        }

        // Build getUpdates URL
        snprintf(url, sizeof(url),
            TELEGRAM_API_BASE "%s/getUpdates?timeout=%d&offset=%lld",
            s_bot_token, TELEGRAM_POLL_TIMEOUT_S, (long long)s_last_update_id);

        // Configure HTTP client
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = http_event_handler,
            .timeout_ms = (TELEGRAM_POLL_TIMEOUT_S + 5) * 1000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to create HTTP client");
            vTaskDelay(pdMS_TO_TICKS(TELEGRAM_RETRY_INTERVAL_MS));
            continue;
        }

        // Reset response buffer
        s_response_len = 0;
        s_response_buffer[0] = '\0';

        // Perform request
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200) {
                s_connected = true;
                process_updates(s_response_buffer);
            } else {
                ESP_LOGW(TAG, "HTTP error: %d", status_code);
                s_connected = false;
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            s_connected = false;
        }

        esp_http_client_cleanup(client);

        // Small delay between polls
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool telegram_send_message(int64_t chat_id, const char* message)
{
    if (strlen(s_bot_token) == 0) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), TELEGRAM_API_BASE "%s/sendMessage", s_bot_token);

    // Build JSON body
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(root, "text", message);

    char* json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_body == NULL) {
        return false;
    }

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(json_body);
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    bool success = (err == ESP_OK && esp_http_client_get_status_code(client) == 200);

    esp_http_client_cleanup(client);
    free(json_body);

    return success;
}

bool telegram_is_connected(void)
{
    return s_connected;
}

bool telegram_set_token(const char* token)
{
    if (token == NULL || strlen(token) >= sizeof(s_bot_token)) {
        return false;
    }

    strncpy(s_bot_token, token, sizeof(s_bot_token) - 1);
    s_bot_token[sizeof(s_bot_token) - 1] = '\0';

    // TODO: Store in NVS
    ESP_LOGI(TAG, "Bot token set");
    return true;
}
