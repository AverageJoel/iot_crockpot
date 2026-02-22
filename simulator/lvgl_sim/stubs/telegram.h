/**
 * @file telegram.h
 * @brief Telegram bot client for the PC simulator.
 *
 * Token is read from the SIM_TELEGRAM_TOKEN environment variable.
 * If the variable is unset or empty, Telegram is silently disabled.
 *
 * Commands:
 *   /status   — current state, temperature, uptime, relay, schedule
 *   /off      — turn crockpot off
 *   /warm     — set WARM
 *   /low      — set LOW
 *   /high     — set HIGH
 *   /schedule — list presets with inline buttons
 *   /log      — export CSV and send filename
 *   /help     — list available commands
 */

#ifndef TELEGRAM_H
#define TELEGRAM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Telegram client.
 *
 * Reads SIM_TELEGRAM_TOKEN.  Logs the bot username on success.
 * No-op if token is unset.
 */
void telegram_init(void);

/**
 * @brief Poll for new messages (non-blocking).
 *
 * Call every ~2 seconds from a timer (not every frame).
 * Issues a getUpdates with timeout=0 and processes any pending messages.
 */
void telegram_poll(void);

/** Shut down mongoose manager and free resources. */
void telegram_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEGRAM_H */
