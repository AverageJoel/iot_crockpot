/**
 * @file nvs_config.h
 * @brief NVS-backed persistent configuration helpers
 *
 * Simple wrappers around the ESP-IDF NVS API for reading and writing
 * string configuration values. Used by wifi.c and telegram.c.
 *
 * Priority for credentials at runtime:
 *   1. NVS (persists across reboots, set at runtime)
 *   2. Kconfig defaults (menuconfig → IoT Crockpot, compile-time)
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// NVS namespaces
#define NVS_NS_WIFI     "wifi"
#define NVS_NS_TELEGRAM "telegram"

// NVS keys
#define NVS_KEY_WIFI_SSID     "ssid"
#define NVS_KEY_WIFI_PASSWORD "password"
#define NVS_KEY_TG_TOKEN      "token"
#define NVS_KEY_TG_CHAT_ID    "chat_id"

/**
 * @brief Read a string value from NVS
 *
 * @param ns        NVS namespace (e.g. NVS_NS_WIFI)
 * @param key       Key name
 * @param buf       Output buffer
 * @param buf_len   Output buffer size
 * @return true if key exists and value is non-empty, false otherwise
 */
bool nvs_config_read_str(const char* ns, const char* key, char* buf, size_t buf_len);

/**
 * @brief Write a string value to NVS
 *
 * @param ns        NVS namespace
 * @param key       Key name
 * @param value     Value to store
 * @return true on success
 */
bool nvs_config_write_str(const char* ns, const char* key, const char* value);

/**
 * @brief Erase a key from NVS
 *
 * @param ns    NVS namespace
 * @param key   Key to erase
 * @return true on success (including key not found)
 */
bool nvs_config_erase_key(const char* ns, const char* key);

#ifdef __cplusplus
}
#endif

#endif // NVS_CONFIG_H
