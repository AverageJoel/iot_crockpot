/**
 * @file crockpot.h
 * @brief Core crockpot state machine and control API
 *
 * Interface-agnostic API for controlling crockpot state.
 * Used by Telegram, local display, and any future interfaces.
 */

#ifndef CROCKPOT_H
#define CROCKPOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crockpot operating states
 */
typedef enum {
    CROCKPOT_OFF = 0,
    CROCKPOT_WARM,
    CROCKPOT_LOW,
    CROCKPOT_HIGH
} crockpot_state_t;

/**
 * @brief Complete crockpot status
 */
typedef struct {
    crockpot_state_t state;
    float temperature_f;
    uint32_t uptime_seconds;
    bool wifi_connected;
    bool sensor_error;
} crockpot_status_t;

/**
 * @brief Initialize the crockpot control system
 *
 * Must be called before any other crockpot functions.
 * Initializes the state machine, temperature sensor, and relay.
 *
 * @return true on success, false on initialization failure
 */
bool crockpot_init(void);

/**
 * @brief Get current crockpot status
 *
 * Thread-safe function to retrieve complete status.
 *
 * @return Current status structure
 */
crockpot_status_t crockpot_get_status(void);

/**
 * @brief Set crockpot operating state
 *
 * Thread-safe function to change crockpot state.
 * Updates relay output accordingly.
 *
 * @param state Desired operating state
 * @return true on success, false on failure
 */
bool crockpot_set_state(crockpot_state_t state);

/**
 * @brief Convert state enum to human-readable string
 *
 * @param state State to convert
 * @return Static string representation (e.g., "OFF", "WARM", "LOW", "HIGH")
 */
const char* crockpot_state_to_string(crockpot_state_t state);

/**
 * @brief Parse string to state enum
 *
 * Case-insensitive parsing of state strings.
 *
 * @param str String to parse (e.g., "off", "WARM", "Low")
 * @param out Pointer to store parsed state
 * @return true if parsing succeeded, false otherwise
 */
bool crockpot_state_from_string(const char* str, crockpot_state_t* out);

/**
 * @brief Main control loop task
 *
 * FreeRTOS task that runs the main control loop.
 * Handles temperature monitoring, safety checks, and relay control.
 *
 * @param pvParameters Task parameters (unused)
 */
void crockpot_control_task(void* pvParameters);

/**
 * @brief Safety temperature limit in Fahrenheit
 *
 * If temperature exceeds this value, crockpot auto-shuts off.
 */
#define CROCKPOT_SAFETY_TEMP_F 300.0f

/**
 * @brief Control loop interval in milliseconds
 */
#define CROCKPOT_CONTROL_INTERVAL_MS 1000

#ifdef __cplusplus
}
#endif

#endif // CROCKPOT_H
