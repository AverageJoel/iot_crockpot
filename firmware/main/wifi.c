/**
 * @file wifi.c
 * @brief WiFi connection management implementation
 */

#include "wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char* TAG = "wifi";

// Event group for WiFi status
static EventGroupHandle_t s_wifi_event_group = NULL;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Current status
static wifi_status_t s_status = WIFI_STATUS_DISCONNECTED;

// Retry counter
static int s_retry_count = 0;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                s_status = WIFI_STATUS_CONNECTING;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_retry_count < WIFI_MAX_RETRY) {
                    ESP_LOGI(TAG, "Disconnected, retrying (%d/%d)...",
                             s_retry_count + 1, WIFI_MAX_RETRY);
                    s_retry_count++;
                    s_status = WIFI_STATUS_CONNECTING;
                    esp_wifi_connect();
                } else {
                    ESP_LOGW(TAG, "Failed to connect after %d attempts", WIFI_MAX_RETRY);
                    s_status = WIFI_STATUS_ERROR;
                    if (s_wifi_event_group) {
                        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    }
                }
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_status = WIFI_STATUS_CONNECTED;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

bool wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false;
    }

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return false;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi initialized");
    return true;
}

bool wifi_connect(void)
{
    ESP_LOGI(TAG, "Connecting to WiFi...");

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    // TODO: Load credentials from NVS
    // For now, use defaults from sdkconfig
    strncpy((char*)wifi_config.sta.ssid, WIFI_DEFAULT_SSID,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_DEFAULT_PASS,
            sizeof(wifi_config.sta.password) - 1);

    if (strlen((char*)wifi_config.sta.ssid) == 0) {
        ESP_LOGE(TAG, "WiFi SSID not configured");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_retry_count = 0;
    s_status = WIFI_STATUS_CONNECTING;

    return true;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    }

    return false;
}

wifi_status_t wifi_get_status(void)
{
    return s_status;
}

bool wifi_is_connected(void)
{
    return s_status == WIFI_STATUS_CONNECTED;
}

bool wifi_get_ip_string(char* buf, size_t buf_len)
{
    if (!wifi_is_connected() || buf == NULL || buf_len == 0) {
        return false;
    }

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }

    snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}

void wifi_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting WiFi");
    esp_wifi_disconnect();
    s_status = WIFI_STATUS_DISCONNECTED;
}

bool wifi_set_credentials(const char* ssid, const char* password)
{
    // TODO: Store in NVS
    ESP_LOGI(TAG, "Setting WiFi credentials (NVS storage not yet implemented)");
    return true;
}
