/**
 * @file wifi.h
 * @brief WiFi connection management
 *
 * Handles WiFi initialization, connection, and reconnection.
 */

#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR
} wifi_status_t;

/**
 * @brief Initialize WiFi subsystem
 *
 * Initializes NVS, netif, and event loop.
 * Must be called before wifi_connect().
 *
 * @return true on success, false on failure
 */
bool wifi_init(void);

/**
 * @brief Connect to configured WiFi network
 *
 * Attempts to connect using credentials from NVS or defaults.
 * Non-blocking - use wifi_wait_connected() or wifi_get_status().
 *
 * @return true if connection attempt started, false on failure
 */
bool wifi_connect(void);

/**
 * @brief Wait for WiFi connection
 *
 * Blocks until connected or timeout.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if connected, false if timed out
 */
bool wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief Get current WiFi status
 *
 * @return Current connection status
 */
wifi_status_t wifi_get_status(void);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Get IP address string
 *
 * @param buf Buffer to store IP string
 * @param buf_len Buffer length
 * @return true if IP address retrieved, false if not connected
 */
bool wifi_get_ip_string(char* buf, size_t buf_len);

/**
 * @brief Disconnect from WiFi
 */
void wifi_disconnect(void);

/**
 * @brief Set WiFi credentials
 *
 * Stores credentials in NVS for persistent storage.
 *
 * @param ssid Network SSID
 * @param password Network password
 * @return true on success
 */
bool wifi_set_credentials(const char* ssid, const char* password);

// Default credentials (for development - use NVS in production)
#define WIFI_DEFAULT_SSID ""
#define WIFI_DEFAULT_PASS ""

// Connection timeout
#define WIFI_CONNECT_TIMEOUT_MS 30000

// Maximum reconnection attempts before giving up
#define WIFI_MAX_RETRY 5

#ifdef __cplusplus
}
#endif

#endif // WIFI_H
