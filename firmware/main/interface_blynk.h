/**
 * @file interface_blynk.h
 * @brief Blynk IoT platform interface (STUB - future implementation)
 *
 * Placeholder for future Blynk integration.
 * Blynk provides a mobile app interface for IoT devices.
 */

#ifndef INTERFACE_BLYNK_H
#define INTERFACE_BLYNK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Blynk interface
 *
 * @return true on success, false if not configured
 */
bool blynk_init(void);

/**
 * @brief Main Blynk task
 *
 * FreeRTOS task for Blynk communication.
 *
 * @param pvParameters Task parameters (unused)
 */
void blynk_task(void* pvParameters);

/**
 * @brief Check if Blynk is connected
 *
 * @return true if connected to Blynk cloud
 */
bool blynk_is_connected(void);

/**
 * @brief Set Blynk auth token
 *
 * @param token Blynk auth token
 * @return true on success
 */
bool blynk_set_token(const char* token);

#ifdef __cplusplus
}
#endif

#endif // INTERFACE_BLYNK_H
