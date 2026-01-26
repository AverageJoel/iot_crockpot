/**
 * @file telegram.h
 * @brief Telegram bot interface for remote control
 *
 * Implements Telegram Bot API long polling for receiving commands
 * and sending status updates.
 */

#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Telegram bot interface
 *
 * Loads bot token from NVS and prepares HTTP client.
 *
 * @return true on success, false if token not configured
 */
bool telegram_init(void);

/**
 * @brief Main Telegram task
 *
 * FreeRTOS task that handles:
 * - Long polling for updates (getUpdates)
 * - Command parsing
 * - Response sending (sendMessage)
 *
 * @param pvParameters Task parameters (unused)
 */
void telegram_task(void* pvParameters);

/**
 * @brief Send a message to a specific chat
 *
 * @param chat_id Telegram chat ID
 * @param message Message text to send
 * @return true on success
 */
bool telegram_send_message(int64_t chat_id, const char* message);

/**
 * @brief Check if Telegram is configured and connected
 *
 * @return true if bot token is set and last poll succeeded
 */
bool telegram_is_connected(void);

/**
 * @brief Set Telegram bot token
 *
 * Stores token in NVS for persistent storage.
 *
 * @param token Bot token from @BotFather
 * @return true on success
 */
bool telegram_set_token(const char* token);

// Long polling timeout in seconds
#define TELEGRAM_POLL_TIMEOUT_S 30

// Update check interval when WiFi is disconnected
#define TELEGRAM_RETRY_INTERVAL_MS 10000

// Maximum message length
#define TELEGRAM_MAX_MESSAGE_LEN 4096

#ifdef __cplusplus
}
#endif

#endif // TELEGRAM_H
