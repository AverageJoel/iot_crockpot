/**
 * @file nvs_config.c
 * @brief NVS-backed persistent configuration helpers
 *
 * Requires nvs_flash_init() to have been called before any reads/writes.
 * wifi_init() already calls nvs_flash_init(), so these helpers are safe
 * to call after wifi_init() has run.
 */

#include "nvs_config.h"

#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "nvs_config";

bool nvs_config_read_str(const char* ns, const char* key, char* buf, size_t buf_len)
{
    if (ns == NULL || key == NULL || buf == NULL || buf_len == 0) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Namespace doesn't exist yet — not an error, just not provisioned
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", ns, esp_err_to_name(err));
        return false;
    }

    buf[0] = '\0';
    size_t len = buf_len;
    err = nvs_get_str(handle, key, buf, &len);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s/%s) failed: %s", ns, key, esp_err_to_name(err));
        return false;
    }

    return strlen(buf) > 0;
}

bool nvs_config_write_str(const char* ns, const char* key, const char* value)
{
    if (ns == NULL || key == NULL || value == NULL) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", ns, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(%s/%s) failed: %s", ns, key, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit(%s) failed: %s", ns, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Saved %s/%s to NVS", ns, key);
    return true;
}

bool nvs_config_erase_key(const char* ns, const char* key)
{
    if (ns == NULL || key == NULL) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);

    // ESP_ERR_NVS_NOT_FOUND is fine — key was already absent
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}
