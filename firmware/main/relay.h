/**
 * @file relay.h
 * @brief Relay/SSR control interface
 *
 * Abstract interface for relay/solid-state relay control.
 * Handles the high-voltage switching for crockpot heating element.
 */

#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include "crockpot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay channel identifiers
 *
 * Multiple channels allow for different heat levels
 * or future expansion (e.g., separate warm element).
 */
typedef enum {
    RELAY_CHANNEL_MAIN = 0,     // Main heating element
    RELAY_CHANNEL_COUNT
} relay_channel_t;

/**
 * @brief Initialize relay control
 *
 * Configures GPIO pins for relay control.
 * Sets all relays to OFF state on init.
 *
 * @return true on success, false on failure
 */
bool relay_init(void);

/**
 * @brief Set relay state for a channel
 *
 * @param channel Relay channel to control
 * @param on true to turn relay on, false to turn off
 * @return true on success, false on invalid channel
 */
bool relay_set(relay_channel_t channel, bool on);

/**
 * @brief Get current relay state
 *
 * @param channel Relay channel to query
 * @return true if relay is on, false if off or invalid channel
 */
bool relay_get(relay_channel_t channel);

/**
 * @brief Turn off all relays
 *
 * Emergency shutoff function.
 */
void relay_all_off(void);

/**
 * @brief Apply crockpot state to relays
 *
 * Translates crockpot state to appropriate relay settings.
 *
 * @param state Crockpot state to apply
 * @return true on success
 */
bool relay_apply_state(crockpot_state_t state);

// GPIO pins for relay control (configurable)
// TODO: Move to menuconfig
#define RELAY_MAIN_GPIO 5

// Relay active level (some relays are active-low)
#define RELAY_ACTIVE_HIGH 1

#ifdef __cplusplus
}
#endif

#endif // RELAY_H
